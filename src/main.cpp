#include <atomic>
#include <csignal>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

#include <tgbot/net/TgLongPoll.h>
#include <tgbot/tgbot.h>
#include <tgbot/tools/StringTools.h>
#include <tgbot/types/InputFile.h>
#include <tgbot/types/Message.h>

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
  string token(getenv("TOKEN"));
  cout << "Token: " << token << endl;
  Bot bot(token);

  struct Job { int64_t chatId; string url; string user; };
  queue<Job> q;
  mutex m;
  condition_variable cv;
  bool stopping = false;

  auto process = [&](const Job &job) {
    string url = job.url;
    cout << "got: " << url << " from id: " << job.user << " username: " << job.user << " name: " << job.user << endl;

    string qurl = shell_quote(url);
    string ofmt = "%(title)s.%(ext)s";
    string flags =
        "-f \"bestvideo[ext=mp4]+bestaudio[ext=m4a]/best[ext=mp4]/best\" "
        "--merge-output-format mp4 --no-playlist --no-progress -o \"" +
        ofmt + "\" ";

    string getname_cmd = "youtube-dl --get-filename " + flags + qurl;
    string download_cmd = "youtube-dl " + flags + qurl;

    cout << "Getting the filename" << endl;
    FILE *pipe = popen(getname_cmd.c_str(), "r");
    if (!pipe) {
      cerr << "filename popen failed" << endl;
      return;
    }

    char buffer[4096];
    string filename;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      filename = trim_newline(buffer);
      cout << "filename: " << filename << endl;
    }
    int ret = pclose(pipe);
    cout << "Exit code: " << ret << endl;
    if (filename.empty()) {
      cerr << "empty filename" << endl;
      return;
    }

    cout << "Downloading the video" << endl;
    pipe = popen(download_cmd.c_str(), "r");
    if (!pipe) {
      cerr << "download popen failed" << endl;
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      cout << buffer;
    }
    ret = pclose(pipe);
    cout << "Exit code: " << ret << endl;

    ifstream test(filename);
    if (!test) {
      cerr << "file not found: " << filename << endl;
      return;
    }

    string outDir = "/tmp/tgbotencodes";
    try {
      filesystem::create_directories(outDir);
    } catch (const exception &e) {
      cerr << "cannot create output dir: " << e.what() << endl;
      return;
    }

    string basename = filesystem::path(filename).filename();
    string outPath = outDir + "/" + basename;

    cout << "Re-encoding to " << outPath << " with x264 CRF 21, preset slow; libfdk_aac 128k" << endl;

    string qin = shell_quote(filename);
    string qout = shell_quote(outPath);
    string enc_cmd = "ffmpeg -y -i " + qin +
                     " -c:v libx264 -crf 21 -preset slow "
                     "-c:a libfdk_aac -b:a 128k -movflags +faststart " +
                     qout + " 2>&1";
    pipe = popen(enc_cmd.c_str(), "r");
    if (!pipe) {
      cerr << "encode popen failed" << endl;
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      cout << buffer;
    }
    ret = pclose(pipe);
    cout << "ffmpeg exit code: " << ret << endl;
    if (ret != 0) {
      cerr << "encoding failed" << endl;
      return;
    }

    try {
      filesystem::remove(filename);
    } catch (const exception &e) {
      cerr << "warning: could not remove original file " << filename << ": " << e.what() << endl;
    }

    cout << "Sending video" << endl;
    bot.getApi().sendVideo(job.chatId, InputFile::fromFile(outPath, "video/mp4"));

    cout << "Video info:" << endl;
    string video_info_cmd = "ffmpeg -hide_banner -i " + shell_quote(outPath) + " 2>&1";
    pipe = popen(video_info_cmd.c_str(), "r");
    if (!pipe) {
      cerr << "ffmpeg info popen failed" << endl;
      return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      cout << buffer;
    }
    ret = pclose(pipe);
    cout << "Exit code: " << ret << endl;
  };

  unsigned n = thread::hardware_concurrency();
  if (n == 0) { n = 2; }
  vector<thread> workers;
  for (unsigned i = 0; i < n; i++) {
    workers.emplace_back([&](){
      while (true) {
        Job job;
        {
          unique_lock<mutex> lk(m);
          cv.wait(lk, [&]{ return stopping || !q.empty(); });
          if (stopping && q.empty()) { return; }
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
    {
      lock_guard<mutex> lk(m);
      q.push(job);
    }
    cv.notify_one();
  });

  signal(SIGINT, on_sigint);

  try {
    cout << "Bot username: " << bot.getApi().getMe()->username << endl;
    bot.getApi().deleteWebhook();
    TgLongPoll longPoll(bot);
    while (true) {
      longPoll.start();
    }
  } catch (exception &e) {
    cout << "error: " << e.what() << endl;
  }

  {
    lock_guard<mutex> lk(m);
    stopping = true;
  }
  cv.notify_all();
  for (auto &t : workers) { if (t.joinable()) { t.join(); } }

  return 0;
}

