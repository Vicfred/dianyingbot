#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <tgbot/net/TgLongPoll.h>
#include <tgbot/tgbot.h>
#include <tgbot/tools/StringTools.h>
#include <tgbot/types/InputFile.h>
#include <tgbot/types/Message.h>

#include "spdlog/spdlog.h"

using namespace std;
using namespace TgBot;

static atomic<bool> stopping{false};

static void on_sigint(int) {
  stopping.store(true);
  _Exit(0);
}

static string trim_newline(const string &s) {
  string t = s;
  while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) {
    t.pop_back();
  }
  return t;
}

static string shell_quote(const string &s) {
  string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

int main() {
  spdlog::set_level(spdlog::level::info);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

  string token(getenv("TOKEN"));
  spdlog::info("Token: {}", token);
  Bot bot(token);

  struct Job {
    int64_t chatId;
    string url;
    string user;
    int64_t userId;
  };
  queue<Job> q;
  mutex m;
  condition_variable cv;
  bool stopping = false;

  auto process = [&](const Job &job) {
    string url = job.url;
    spdlog::info("got: {} from username: {} user_id: {}", url, job.user, job.userId);

    string qurl = shell_quote(url);
    string ofmt = "%(title)s.%(ext)s";
    string flags =
        "-f \"bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best\" "
        "--merge-output-format mp4 --no-playlist --no-progress -o \"" +
        ofmt + "\" ";

    string getname_cmd = "youtube-dl --get-filename " + flags + qurl;
    string download_cmd = "youtube-dl " + flags + qurl;

    spdlog::info("Getting the filename");
    FILE *pipe = popen(getname_cmd.c_str(), "r");
    if (!pipe) {
      spdlog::error("filename popen failed");
      return;
    }

    char buffer[4096];
    string filename;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      filename = trim_newline(buffer);
      spdlog::info("filename: {}", filename);
    }
    int ret = pclose(pipe);
    spdlog::debug("get-filename exit code: {}", ret);
    if (filename.empty()) {
      spdlog::error("empty filename");
      return;
    }

    spdlog::info("Downloading the video");
    pipe = popen(download_cmd.c_str(), "r");
    if (!pipe) {
      spdlog::error("download popen failed");
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      spdlog::debug("dl: {}", string(buffer));
    }
    ret = pclose(pipe);
    spdlog::debug("download exit code: {}", ret);

    ifstream test(filename);
    if (!test) {
      spdlog::error("file not found: {}", filename);
      return;
    }

    string outDir = "/tmp/tgbotencodes";
    try {
      filesystem::create_directories(outDir);
    } catch (const exception &e) {
      spdlog::error("cannot create output dir: {}", e.what());
      return;
    }

    string basename = filesystem::path(filename).filename();
    string outPath = outDir + "/" + basename;

    spdlog::info(
        "Re-encoding to {} with x264 CRF 23, preset slow; libfdk_aac 128k",
        outPath);

    string qin = shell_quote(filename);
    string qout = shell_quote(outPath);
    string enc_cmd = "ffmpeg -y -i " + qin +
                     " -c:v libx264 -crf 23 -preset slow "
                     "-c:a libfdk_aac -b:a 128k -movflags +faststart " +
                     qout + " 2>&1";
    pipe = popen(enc_cmd.c_str(), "r");
    if (!pipe) {
      spdlog::error("encode popen failed");
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      spdlog::debug("ffmpeg: {}", string(buffer));
    }
    ret = pclose(pipe);
    spdlog::debug("ffmpeg exit code: {}", ret);
    if (ret != 0) {
      spdlog::error("encoding failed");
      return;
    }

    try {
      filesystem::remove(filename);
    } catch (const exception &e) {
      spdlog::error("could not remove original file {}: {}", filename,
                    e.what());
    }

    spdlog::info("Sending video");
    bot.getApi().sendVideo(job.chatId,
                           InputFile::fromFile(outPath, "video/mp4"));

    spdlog::debug("Video info:");
    string video_info_cmd =
        "ffmpeg -hide_banner -i " + shell_quote(outPath) + " 2>&1";
    pipe = popen(video_info_cmd.c_str(), "r");
    if (!pipe) {
      spdlog::error("ffmpeg info popen failed");
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      spdlog::debug("info: {}", string(buffer));
    }
    ret = pclose(pipe);
    spdlog::debug("info exit code: {}", ret);
  };

  unsigned n = thread::hardware_concurrency();
  if (n == 0) {
    n = 2;
  }
  vector<thread> workers;
  for (unsigned i = 0; i < n; i++) {
    workers.emplace_back([&]() {
      while (true) {
        Job job;
        {
          unique_lock<mutex> lk(m);
          cv.wait(lk, [&] { return stopping || !q.empty(); });
          if (stopping && q.empty()) {
            return;
          }
          job = q.front();
          q.pop();
        }
        process(job);
      }
    });
  }

  bot.getEvents().onAnyMessage([&](Message::Ptr message) {
    Job job;
    job.chatId = message->chat->id;
    job.url = message->text;
    job.user = message->from ? message->from->username : string();
    job.userId = message->from->id;
    {
      lock_guard<mutex> lk(m);
      q.push(job);
    }
    cv.notify_one();
    spdlog::info("queued job for chat {}", job.chatId);
  });

  signal(SIGINT, on_sigint);

  try {
    spdlog::info("Bot username: {}", bot.getApi().getMe()->username);
    bot.getApi().deleteWebhook();
    TgLongPoll longPoll(bot);
    while (true) {
      longPoll.start();
    }
  } catch (exception &e) {
    spdlog::error("error: {}", e.what());
  }

  {
    lock_guard<mutex> lk(m);
    stopping = true;
  }
  cv.notify_all();
  for (auto &t : workers) {
    if (t.joinable()) {
      t.join();
    }
  }

  return 0;
}
