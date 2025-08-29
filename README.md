# dianyingbot (電影)

A simple Telegram bot that downloads videos via [youtube-dl](https://github.com/ytdl-org/youtube-dl) (or [yt-dlp](https://github.com/yt-dlp/yt-dlp)) and sends them back to you on Telegram.
The name **dianyingbot** comes from the Chinese word **電影 (diànyǐng)** which means *movie*.

---

## Features

* Accepts any YouTube (or other supported site) link in chat.
* Supports downloads from **Instagram Reels**, **TikTok**, and **小红书 (Xiaohongshu)** in addition to YouTube.
* Re-encodes using **x264** (CRF 21, preset slow) and **libfdk\_aac** (128 kbps).
* Stores final files in `/tmp/tgbotencodes`.
* Sends the encoded video back via Telegram API.
* Multithreaded worker pool: multiple downloads and encodes can be processed in parallel.
* Logging with [spdlog](https://github.com/gabime/spdlog) (debug, info, error levels).

---

## Requirements

* Linux (tested on Gentoo, should work anywhere).
* `ffmpeg` built with `libfdk_aac`.
* `youtube-dl` (or `yt-dlp` recommended).
* C++17 compiler.
* [TgBot C++ Library](https://github.com/reo7sp/tgbot-cpp).
* [spdlog](https://github.com/gabime/spdlog) (header-only).
* CMake ≥ 3.10.

---

## Build

```bash
mkdir build
cd build
cmake ..
make
```

---

## Usage

1. Create a bot via [BotFather](https://core.telegram.org/bots#botfather) and copy your token.
2. Export the token:

   ```bash
   export TOKEN="123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11"
   ```
3. Run the bot:

   ```bash
   ./dianying
   ```
4. Send a YouTube, Instagram Reels, TikTok, or 小红书 link to your bot in Telegram. It will:

   * download,
   * re-encode,
   * store in `/tmp/tgbotencodes/`,
   * and send the video back.

---

## Project Structure

```
src/main.cpp        # bot source
CMakeLists.txt      # build setup
/tmp/tgbotencodes   # output folder for encoded videos
```

---

## Notes

* Telegram bots have a file size limit (currently **50 MB per file** via Bot API).
* Videos larger than this cannot be sent.
* Clean up `/tmp/tgbotencodes` occasionally if disk space is limited.

---

## License

BSD 3-Clause License

---

*dianyingbot* — turning your favorite 電影, Reels, TikToks, and 小红书 videos into Telegram-friendly formats!

