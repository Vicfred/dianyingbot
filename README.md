# dianyingbot (電影)

A simple Telegram bot that downloads videos via [youtube-dl](https://github.com/ytdl-org/youtube-dl) (or [yt-dlp](https://github.com/yt-dlp/yt-dlp)) and sends them back to you on Telegram.
The name **dianyingbot** comes from the Chinese word **電影 (diànyǐng)** which means *movie*.

---

## Features

* Accepts any YouTube (or other supported site) link in chat.
* Supports downloads from **Instagram Reels**, **TikTok**, and **小红书 (Xiaohongshu)** in addition to YouTube.
* Re-encodes using **x264** (CRF 30, preset **fast**) and **libfdk\_aac** (96 kbps).
* Stores final files in `/tmp/tgbotencodes`.
* Sends the encoded video back via Telegram API.
* Multithreaded worker pool: multiple downloads and encodes can be processed in parallel.
* Logging with [spdlog](https://github.com/gabime/spdlog) (debug, info, error levels).
* **Optional: Works with a self-hosted Telegram Bot API server** for faster transfers and higher upload limits (up to **2000 MB** when running in local mode).

---

## Requirements

* Linux (tested on Gentoo; should work on other distros/BSDs with compatible deps).
* `ffmpeg` built with `libfdk_aac`.
* `youtube-dl` **or** `yt-dlp` (recommended).
* C++17 compiler.
* [TgBot C++ Library](https://github.com/reo7sp/tgbot-cpp).
* [spdlog](https://github.com/gabime/spdlog) (header-only).
* CMake ≥ 3.10.
* **Optional (for local API):** [telegram-bot-api](https://github.com/tdlib/telegram-bot-api).

---

## Environment Variables

The bot reads the following:

* `DIANYINGTOKEN` (required): your bot token from BotFather.
* `BOT_API_URL` (optional): base URL of a self-hosted Bot API server, e.g. `http://127.0.0.1:8081`. If unset, the bot uses the public `https://api.telegram.org`.

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
2. Export the token (and optionally the local API URL):

   ```bash
   export DIANYINGTOKEN="123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11"
   # optional, only if you run a local Bot API server:
   export BOT_API_URL="http://127.0.0.1:8081"
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

## Using a Local Telegram Bot API Server (optional)

Running the official Bot API server locally can remove some cloud limits and speed up file operations:

* Upload files up to **2000 MB** (vs. 50 MB on the public Bot API).
* Download files without a size limit.

### Quick start

1. Build or install `telegram-bot-api` (see the repo README).
2. Run it in local mode (pick your own port/paths):

   ```bash
   telegram-bot-api \
     --local \
     --api-id=<YOUR_API_ID> \
     --api-hash=<YOUR_API_HASH> \
     --http-port=8081 \
     --dir=/var/lib/telegram-bot-api
   ```
3. Point the bot at your server:

   ```bash
   export BOT_API_URL="http://127.0.0.1:8081"
   ./dianying
   ```

The TgBot client will automatically use `BOT_API_URL` if it’s set; otherwise it falls back to the public API.

---

## Project Structure

```
src/main.cpp        # bot source
CMakeLists.txt      # build setup
/tmp/tgbotencodes   # output folder for encoded videos
```

---

## Notes

* On the public Bot API, bots can send files up to **50 MB**. For larger uploads, use the local Bot API server described above.
* Clean up `/tmp/tgbotencodes` occasionally if disk space is limited.

---

## License

BSD 3-Clause License

---

*dianyingbot* — turning your favorite 電影, Reels, TikToks, and 小红书 videos into Telegram-friendly formats!

