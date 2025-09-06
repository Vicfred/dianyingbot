#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/color.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <tgbot/Bot.h>
#include <tgbot/types/LinkPreviewOptions.h>
#include <thread>

#include "spdlog/spdlog.h"
#include <tgbot/tgbot.h>

using namespace std;

const string instructionsHtml =
    R"( <b>How to use this bot (no commands needed)</b>

- Just send me a video link. That's it.
- Supported: YouTube, Instagram, TikTok, Xiaohongshu.
- I'll fetch the video and send it back to you.
- No /start or other commands are required.

<b>Two easy ways</b>
1) Copy the link -> open this chat -> paste the link -> send.
2) From the app's Share menu -> choose Telegram -> pick this bot.

<b>Examples of links</b>
- https://youtu.be/VIDEO_ID
- https://www.instagram.com/reel/XXXXX
- https://www.tiktok.com/@user/video/NNNNN
- https://www.xiaohongshu.com/explore/POST_ID

<b>Notes</b>
- Public posts only; private/age-restricted/protected links will not work.
- Very long videos or region-locked posts may fail.
- Please respect creators' rights and the Terms of Service of each platform.)";

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

  string token(getenv("DIANYINGTOKEN"));
  unique_ptr<TgBot::Bot> bot = make_unique<TgBot::Bot>(token);
  // bot->getApi().logOut();

  TgBot::CurlHttpClient curlHttpClient;
  string local_api_url = getenv("BOT_API_URL");
  if (getenv("BOT_API_URL") != NULL) {
    bot = make_unique<TgBot::Bot>(token, curlHttpClient, local_api_url);
    spdlog::info("Using local api server {}", getenv("BOT_API_URL"));
  }

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
      TgBot::LinkPreviewOptions::Ptr opts =
          make_shared<TgBot::LinkPreviewOptions>();
      opts->isDisabled = true;
      bot->getApi().sendMessage(job.chatId, instructionsHtml, opts, nullptr,
                                nullptr, "HTML");
      return;
    }
    bot->getApi().sendMessage(job.chatId, "I got your request, working on it!");
    this_thread::sleep_for(chrono::seconds(5));

    spdlog::info("Downloading the video");
    pipe = popen(download_cmd.c_str(), "r");
    if (!pipe) {
      bot->getApi().sendMessage(job.chatId, "I failed to download your video.");
      spdlog::error("download popen failed");
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      spdlog::debug("dl: {}", string(buffer));
    }
    ret = pclose(pipe);
    spdlog::debug("download exit code: {}", ret);
    bot->getApi().sendMessage(
        job.chatId,
        "I got your video, I will re-encode it for maximum compatibility~");

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
        "Re-encoding to {} with x265 CRF 28, preset slow; libfdk_aac 96k",
        outPath);

    string qin = shell_quote(filename);
    string qout = shell_quote(outPath);
    string enc_cmd = "ffmpeg -y -i " + qin +
                     " -c:v libx265 -crf 28 -preset slow "
                     "-c:a libfdk_aac -b:a 96k -movflags +faststart " +
                     qout + " 2>&1";
    pipe = popen(enc_cmd.c_str(), "r");
    if (!pipe) {
      spdlog::error("encode popen failed");
      bot->getApi().sendMessage(job.chatId, "Video encoding failed.");
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      spdlog::debug("ffmpeg: {}", string(buffer));
    }
    ret = pclose(pipe);
    spdlog::debug("ffmpeg exit code: {}", ret);
    if (ret != 0) {
      spdlog::error("encoding failed");
      bot->getApi().sendMessage(job.chatId, "Video encoding failed.");
      return;
    }

    try {
      filesystem::remove(filename);
    } catch (const exception &e) {
      spdlog::error("could not remove original file {}: {}", filename,
                    e.what());
    }

    spdlog::info("Sending video");
    bot->getApi().sendMessage(job.chatId,
                              "I'm done with the encoding, I will send it now, "
                              "you should get it soon~");
    try {
      bot->getApi().sendVideo(job.chatId,
                              TgBot::InputFile::fromFile(outPath, "video/mp4"));
    } catch (exception &e) {
      bot->getApi().sendMessage(job.chatId, "Failed to send the video.");
      spdlog::error("Sending video failed, error: {}", e.what());
    }

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

  const set<int64_t> allowed_users = {3376040, 265288934, 1216729714,
                                      6540848155, 1844076108};

  bot->getEvents().onAnyMessage([&](TgBot::Message::Ptr message) {
    Job job;
    job.chatId = message->chat->id;
    job.url = message->text;
    job.user = message->from ? message->from->username : string();
    job.userId = message->from->id;
    spdlog::info("got: {} from username: {} user_id: {}", job.url,
                 fmt::styled(job.user, fmt::fg(fmt::color::deep_pink) |
                                           fmt::emphasis::bold),
                 fmt::styled(job.userId, fmt::fg(fmt::color::deep_pink) |
                                             fmt::emphasis::bold));
    if (allowed_users.count(message->from->id) == 0) {
      return;
    }
    {
      lock_guard<mutex> lk(m);
      q.push(job);
    }
    cv.notify_one();
    spdlog::info("queued job for chat {}", job.chatId);
  });

  signal(SIGINT, on_sigint);

  try {
    spdlog::info("Bot username: {}", bot->getApi().getMe()->username);
    bot->getApi().deleteWebhook();
    TgBot::TgLongPoll longPoll(*bot);
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
