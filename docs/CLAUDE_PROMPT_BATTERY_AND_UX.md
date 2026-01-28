# Claude prompt: Koetori battery + M5 ease of use

**Copy everything below the line into a new chat with Claude when you want to optimize the Koetori project for M5Stick battery life and ease of use.**

---

I have a project called Koetori with two parts:

1. **iOS app** (Swift/SwiftUI): Records audio from the mic or receives it over BLE from an M5Stick, uploads to an API for transcription/categorization, shows results and history.
2. **M5Stick firmware** (Arduino/C++, M5StickC Plus 2, ESP32): Records voice with the built-in mic, streams audio to the iOS app over BLE (or saves to SPIFFS and queues when offline). The iPhone app then uploads to the API.

I want you to go through **both** the iOS app and the M5Stick codebases and propose or implement changes to:

- **Battery life on the M5Stick**  
  Idle drain is currently high (device can die in tens of minutes overnight). Optimize for long idle runtime (hours) while keeping: press A to record, press A again to stop and send over BLE (or queue), and auto-send queue when the app connects. Prefer concrete code changes: e.g. light/deep sleep with wake on button (and optionally BLE), lower CPU when idle, less aggressive BLE advertising, conditional Serial, turning off unused hardware.

- **Ease of use on the M5Stick**  
  The only flow that matters is: **one press = start recording**, **second press = stop and send** (no extra “load” or “wake” tap). If the screen was off, the first press should both wake the screen and start recording so the user never has to tap twice. Button B = wake and/or cancel recording; no menus required for normal use.

**Context and reference docs in the repo:**

- **M5Stick:**
  - `docs/BATTERY_OPTIMIZATION_BRIEF.md` – current power state, what’s been tried, desired directions (sleep, CPU, BLE, Serial, display).
  - `docs/BLE_AUDIO_STREAMING_SPEC.md` – BLE protocol and streaming behavior.
  - `docs/TESTING.md` – how to build and test.
  - Main touch points: `config.h`, `koetori_m5stick.ino` (setup), `recording.ino` (loop, record, save/upload, button handling), `display.ino`, `ble_handler.ino`, `utils.ino` (screen on/off, power helpers).

- **iOS:**
  - Single record button: tap = start, tap again = stop and upload (and show results). History for past memos; BLE receive from M5Stick. No “load” step required.

Please suggest or implement changes that:

1. **M5 battery:** Reduce idle power (sleep, CPU, BLE, Serial, display, other subsystems) with concrete edits and file names, while keeping “A = record, A again = stop and send” and queue/auto-send behavior.
2. **M5 ease of use:** Ensure one press starts recording (and wakes if needed); second press stops and sends. No double-tap to “load” then record.
3. **iOS:** Only change if something clearly helps the above (e.g. BLE or API interaction); otherwise leave as-is.

If you need to assume paths or repo layout: M5Stick code is under a folder like `koetori_m5stick/` (`.ino`, `config.h`, `docs/`); iOS app is under something like `koetori-ios/Koetori/`. Reference the docs above for exact file names and behavior.
