#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <exception>
#include <filesystem>
#include <fmt/color.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <tgbot/Bot.h>
#include <tgbot/net/HttpClient.h>
#include <tgbot/net/Url.h>
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

static string random_hex(size_t bytes) {
  static const char *hex = "0123456789abcdef";
  random_device rd;
  uniform_int_distribution<int64_t> dist(0, 255);
  string s;
  s.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; i++) {
    int64_t v = dist(rd);
    s.push_back(hex[(v >> 4) & 15]);
    s.push_back(hex[v & 15]);
  }
  return s;
}

int main() {
  spdlog::set_level(spdlog::level::info);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");

  string token(getenv("DIANYINGTOKEN"));
  unique_ptr<TgBot::Bot> bot = make_unique<TgBot::Bot>(token);
  // bot->getApi().logOut();

  set<pair<int64_t, int64_t>> seen;
  mutex seenMu;

  TgBot::CurlHttpClient curlHttpClient;
  curlHttpClient._timeout = 600;
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
    string outname = random_hex(16) + ".mp4";
    string flags =
        "-f "
        "\"bestvideo[height<=720][ext=mp4]+bestaudio[ext=m4a]/"
        "best[height<=720][ext=mp4]/best[height<=720]/best\" "
        "--cookies /home/vicfred/brave.filtered.txt"
        "--merge-output-format mp4 --no-playlist --no-progress -o \"" +
        outname + "\" ";
    spdlog::debug("{}", flags);

    string download_cmd = "yt-dlp " + flags + qurl;

    char buffer[4096];
    string filename = outname;

    bot->getApi().sendMessage(job.chatId, "I got your request, working on it!");
    this_thread::sleep_for(chrono::seconds(5));

    spdlog::info("Downloading the video");
    FILE *pipe = popen(download_cmd.c_str(), "r");
    if (!pipe) {
      bot->getApi().sendMessage(job.chatId, "I failed to download your video.");
      spdlog::error("download popen failed");
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      spdlog::debug("dl: {}", string(buffer));
    }
    int ret = pclose(pipe);
    spdlog::debug("download exit code: {}", ret);

    ifstream test(filename);
    if (!test) {
      spdlog::error("file not found: {}", filename);
      return;
    }

    string basename = filesystem::path(filename).filename();

    spdlog::info("Sending video");
    try {
      bot->getApi().sendVideo(
          job.chatId, TgBot::InputFile::fromFile(basename, "video/mp4"));
    } catch (exception &e) {
      bot->getApi().sendMessage(job.chatId, "Failed to send the video.");
      spdlog::error("Sending video failed, error: {}", e.what());
    }

    spdlog::debug("Video info:");
    string video_info_cmd =
        "ffmpeg -hide_banner -i " + shell_quote(basename) + " 2>&1";
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

    try {
      filesystem::remove(filename);
    } catch (const exception &e) {
      spdlog::error("could not remove original file {}: {}", filename,
                    e.what());
    }
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
    if (!message || !message->chat || !message->from) {
      return;
    }
    if (allowed_users.count(message->from->id) == 0) {
      return;
    }
    if (message->text.empty()) {
      return;
    }
    const string &t = message->text;
    if (!(t.rfind("http://", 0) == 0 || t.rfind("https://", 0) == 0)) {
      return;
    }
    pair<int64_t, int64_t> key = {message->chat->id, message->messageId};
    {
      lock_guard<mutex> lk(seenMu);
      if (seen.count(key)) {
        spdlog::info("dup ignored chat {} msg {}", key.first, key.second);
        return;
      }
      seen.insert(key);
      if (seen.size() > 2000) {
        seen.clear();
      }
    }
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
    TgBot::TgLongPoll longPoll(*bot, 100, 600);
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
