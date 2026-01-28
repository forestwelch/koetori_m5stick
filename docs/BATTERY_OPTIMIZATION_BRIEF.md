# Battery optimization brief for Claude Code

**Copy this into a new chat with Claude when you want to go further on battery life.**

---

## User goal

- **Only** supported behavior: **press the M5 button to record, press again to stop, and it sends over BLE to the iPhone app.** Nothing else is required.
- **Problem:** Left overnight in idle, the device died in **~45 minutes**.
- **Objective:** Strip all unnecessary functionality and optimize for battery so idle runtime is much longer (hours, not tens of minutes).

---

## Current state (after an initial pass)

### What was already changed (initial pass)

1. **Screen**
   - Dim after **1 s** (was 2 s), turn **off after 3 s** idle (was 10 s).
   - Idle UI is minimal: BLE status, battery %, one hint “A = REC”. No Gengar sprite, no menu/debug hints, no recording-mode text.
   - Startup no longer draws the Bailey sprite; goes straight to a filled screen then `displayReady()`.

2. **Buttons**
   - **A:** Record / stop (and wake screen). Only user action for the “record → BLE send” flow.
   - **B:** Wake screen; **cancel** current recording if there is one. **No menu** (menu code exists but is never entered).
   - **PWR:** Wake screen only. **No debug menu.**

3. **Main loop**
   - When **idle** (`!isRecording && !hasRecording`): `delay(250)` instead of `delay(100)` to reduce wakeups.
   - When **not idle**: `delay(100)` unchanged.
   - If **BLE connected** and **queue has files**: **auto-send queue** (no need to open a menu). Queue is sent as soon as the app connects.

4. **Unchanged**
   - BLE is always initialized; when disconnected it keeps **advertising** so the iPhone can find the device.
   - CPU is fixed at **80 MHz** (`setCpuFrequencyMhz(80)` in setup).
   - **Serial** at 115200 and multiple `Serial.print*` paths are still active.
   - **Long recording mode** and menu code remain in the tree but are unused (no UI path to them).
   - Recording, BLE streaming (RAM + file paths), queue, SPIFFS, mic, and power/display helpers are unchanged except where above.

### What still runs when “idle”

- **Loop** runs every **250 ms**: `M5.update()`, screen dim/off logic, button checks, then `delay(250)`.
- **BLE**: stack is up; if no client is connected, **advertising** runs (interval/params are default).
- **Display**: after 3 s idle, **off** (`setBrightness(0)` + `sleep()`). No periodic redraw when off.
- **CPU**: **80 MHz** all the time.
- **Serial**: still in use wherever `Serial.*` is called.

So the device is **not** in deep sleep or light sleep; it’s in a busy-poll loop with BLE advertising, which is a plausible cause of high idle current and ~45 min lifetime.

---

## Hardware / stack context

- **Board:** M5StickC Plus 2 (ESP32-based).
- **Arduino/ESP32:** `M5StickCPlus2` library, BLE (BLEDevice, BLEServer, etc.), SPIFFS, built-in mic, display, power APIs.
- **Existing power/display helpers:** `screenOff()` / `screenOn()`, `resetScreenPower()`, `manageScreenPower*()`, `SCREEN_BRIGHTNESS` / `SCREEN_BRIGHTNESS_DIMMED` in `config.h`.

---

## Desired directions for Claude (next steps)

Optimize for **idle** and **minimal UX** (only: press to record, press to stop, BLE send to iPhone). Prefer concrete code/compile-time or runtime changes, not only theory.

1. **Sleep**
   - Use **light sleep** or **deep sleep** when idle (e.g. after display has been off for a few seconds?), with **wake on GPIO** (button A).
   - Keep BLE usability: e.g. wake on BLE connect or accept “first press wakes, second press records” if that’s the only way to get sleep.

2. **CPU**
   - Lower **CPU frequency** when idle (e.g. 80 → 40 MHz, or 10 MHz if supported and BLE still works). Revert to 80 MHz when recording or streaming.

3. **BLE**
   - Increase **advertising interval** (or reduce adv payload) to cut power when no one is connecting, if the stack allows and discovery is still acceptable.

4. **Serial**
   - **#ifdef** or compile-time flag to **disable** or reduce `Serial.*` in a “release” or “battery” build to avoid UART and log overhead when not debugging.

5. **Display**
   - Ensure when “off” we’re not doing redundant draws or唤醒. Prefer **hardware off/sleep** and only touch the display when the user wakes the device or when we’re in record/send flow.

6. **Scope**
   - Identify any other subsystems (e.g. IMU, unused radios, always-on timers) and turn them off or reduce their use when not needed for “record → BLE send”.

---

## File reference (main touch points)

- **`config.h`**  
  `SCREEN_DIM_DELAY`, `IDLE_SCREEN_OFF_DELAY`, `SCREEN_BRIGHTNESS*`, `SAMPLE_RATE`, BLE/audio constants.

- **`koetori_m5stick.ino`**  
  `setup()`: CPU freq, M5.begin (e.g. `internal_imu = false`), init order, **no** `drawBailey()`.

- **`recording.ino`**  
  `loop()`: auto-send queue when BLE+queue, screen dim/off, **A** = record/stop, **B** = wake/cancel, **PWR** = wake, `delay(250)` when idle.

- **`display.ino`**  
  `displayReady()`: minimal idle UI (BLE, battery, one “A = REC” hint). No sprite, no menu/debug hints.

- **`ble_handler.ino`**  
  BLE init, advertising start/stop, `streamViaBLE` / `streamViaBLEFromSamples`, connection state.

- **`utils.ino`**  
  `screenOff()` / `screenOn()`, `resetScreenPower()`, `manageScreenPower*()`.

- **`menus.ino`**  
  Still contains `showMenu()`, `showDebugMenu()`, etc.; they are **no longer** called from `loop()`.

---

## Success criteria (for the user)

- Same behavior: **press A to record, press A to stop → send over BLE to iPhone** (and queue when offline, auto-send when app connects).
- **Idle drain** much lower than “dead in ~45 minutes” — target: many hours of idle runtime.
- No new “required” steps (e.g. no mandatory menu or multi-button flows for the primary use case).

Use this brief to reason about where the power goes and to propose concrete patches (with file names and, where useful, code snippets or pseudocode) for the M5 Stick firmware.
