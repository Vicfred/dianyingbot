# 🎬 dianyingbot (電影)

A simple Telegram bot that downloads videos via [youtube-dl](https://github.com/ytdl-org/youtube-dl) (or [yt-dlp](https://github.com/yt-dlp/yt-dlp)) and sends them back to you on Telegram.
The name **dianyingbot** comes from the Chinese word **電影 (diànyǐng)** which means *movie*.

---

## ✨ Features

* Accepts any YouTube (or other supported site) link in chat.
* Downloads the video with `youtube-dl`/`yt-dlp`.
* Re-encodes using **x264** (CRF 21, preset slow) and **libfdk\_aac** (128 kbps).
* Stores final files in `/tmp/tgbotencodes`.
* Sends the encoded video back via Telegram API.

---

## 📦 Requirements

* Linux (tested on Gentoo, should work anywhere).
* `ffmpeg` built with `libfdk_aac`.
* `youtube-dl` (or `yt-dlp` recommended).
* C++17 compiler.
* [TgBot C++ Library](https://github.com/reo7sp/tgbot-cpp).
* CMake ≥ 3.10.

---

## 🔧 Build

```bash
mkdir build
cd build
cmake ..
make
```

---

## 🚀 Usage

1. Create a bot via [BotFather](https://core.telegram.org/bots#botfather) and copy your token.
2. Export the token:

   ```bash
   export TOKEN="123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11"
   ```
3. Run the bot:

   ```bash
   ./dianying
   ```
4. Send a YouTube link to your bot in Telegram.
   It will:

   * download,
   * re-encode,
   * store in `/tmp/tgbotencodes/`,
   * and send the video back.

---

## 📂 Project Structure

```
src/main.cpp        # bot source
CMakeLists.txt      # build setup
/tmp/tgbotencodes   # output folder for encoded videos
```

---

## ⚠️ Notes

* Telegram bots have a file size limit (currently **50 MB per file** via Bot API).
* Videos larger than this cannot be sent.
* Clean up `/tmp/tgbotencodes` occasionally if disk space is limited.

---

## 📝 License

MIT

---

💡 *dianyingbot* — turning your favorite **電影** into Telegram-friendly videos!

