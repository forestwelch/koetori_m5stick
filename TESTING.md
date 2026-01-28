# Testing Koetori M5Stick BLE Firmware

This firmware streams audio from the M5StickC PLUS2 to the Koetori iOS app over BLE. The iPhone handles upload to the Koetori API.

## 1. Arduino / Build Environment

### Option A: Arduino IDE

1. **ESP32 boards**
   - File → Preferences → Additional Board Manager URLs:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Tools → Board → Boards Manager → search “esp32” → install **esp32 by Espressif Systems**.

2. **M5StickC PLUS2**
   - Boards Manager: add
     ```
     https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
     ```
   - Install “M5Stack by M5Stack official”.
   - Or install **M5StickCPlus2** from Sketch → Include Library → Manage Libraries.

3. **Board selection**
   - Tools → Board → **M5Stack Arduino → M5StickC PLUS2** (or equivalent in the M5Stack package).

4. **Port**
   - Connect the stick via USB; Tools → Port → choose the correct serial port.

### Option B: Arduino CLI

```bash
# Add ESP32 and M5 indices
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli config add board_manager.additional_urls https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json

# Install cores
arduino-cli core update-index
arduino-cli core install esp32:esp32
# If using M5 package:
arduino-cli core install m5stack:esp32

# Build (from repo root, parent of koetori_m5stick/)
arduino-cli compile koetori_m5stick

# Upload (replace /dev/cu.usbserial-XXX with your port)
arduino-cli upload -p /dev/cu.usbserial-XXX koetori_m5stick
```

### Option C: PlatformIO

Create a `platformio.ini` in the sketch folder or use a project that references this sketch and uses:

- Board: `m5stack-stick-c-plus2` or the exact M5StickC PLUS2 board id from the M5/ESP32 package.
- Framework: `arduino`.
- lib_deps: M5StickCPlus2 (or the library that provides `M5StickCPlus2.h`).

There is no full emulator for the real M5 stick (mic + display + buttons). You can run **ESP32 BLE-only** code on a generic ESP32 in the Arduino IDE or PlatformIO to test BLE advertising/characteristics without the M5 hardware.

## 2. Config and Build

1. Copy and edit secrets:

   ```bash
   cp secrets.h.example secrets.h
   ```

   Set `DEVICE_ID` (e.g. `"m5stick-001"`). The stick advertises as **Koetori-M5-&lt;DEVICE_ID&gt;**.

2. Open `koetori_m5stick` in Arduino IDE (or run `arduino-cli compile` from the parent directory as above).

3. Build and upload to the M5StickC PLUS2 over USB.

## 3. BLE Testing Strategy

### Step 1: Advertising

- Power the stick. It should advertise as **Koetori-M5-&lt;DEVICE_ID&gt;**.
- On iOS: open a generic BLE scanner (e.g. “LightBlue”, “nRF Connect”) and confirm the name and that it exposes a service with UUID `4fafc201-1fb5-459e-8fcc-c5c9c331914b`.

### Step 2: Connect from phone

- In the scanner app, connect to the device.
- Discover the service and its characteristics:
  - **Audio**: `beb5483e-36e1-4688-b7f5-ea07361b26a8` (notify/read)
  - **Control**: `beb5483e-36e1-4688-b7f5-ea07361b26a9` (read/write/notify)
  - **Status**: `beb5483e-36e1-4688-b7f5-ea07361b26aa` (read/notify)
- Subscribe to Audio and Control notifications to see traffic during a recording.

### Step 3: Record and stream

- With the stick connected via BLE (scanner or Koetori iOS app):
  - Press **A** to start recording.
  - Press **A** again to stop.
- You should see:
  - On **Control**: `START:<timestamp>:16000:1`, then after the transfer `END:<chunk_count>`.
  - On **Audio**: notifications with 512-byte chunks (2-byte chunk index + 510 bytes PCM).
- On the stick display: “SENDING” then “SENT” (and optionally category/confidence if the app writes `SUCCESS:…` to Control).

### Step 4: Koetori iOS app

- Use the Koetori iOS app as the central: scan, connect to **Koetori-M5-&lt;DEVICE_ID&gt;**.
- The app should discover the same service/characteristics, receive audio chunks, assemble WAV, and upload to the Koetori API.
- After a recording, the app can write `SUCCESS:category:confidence` to the Control characteristic so the stick can show the result.

### Step 5: Queue when disconnected

- Disconnect the phone (or turn BLE off), record on the stick, stop. The stick should show “SAVED” and “Open app to sync”.
- Reconnect the app, open Menu → **SEND N** to sync the queue over BLE.

## 4. Serial Monitor

- Baud rate: **115200**.
- You should see:
  - `BLE initialized, advertising`
  - `BLE client connected` / `BLE client disconnected`
  - `BLE streamed /rec.wav: N chunks`
  - Recording progress and errors.

Use this to confirm BLE connection, streaming, and any ERROR messages on the Control characteristic.

## 5. Common issues

- **Stick not visible in BLE scanners**
  - Check that BLE is enabled and no other process is holding the controller.
  - Restart the stick; confirm “BLE initialized, advertising” on serial.
- **“CONNECT” / “Open Koetori app” when sending**
  - No central is connected. Connect with the scanner or Koetori app first, then trigger SEND or record.
- **Chunks never finish / SENT never appears**
  - Watch serial for “BLE disconnected during stream” or ERROR on Control.
  - Reduce background load on the phone; keep the app in foreground while streaming.
- **iPhone app doesn’t see the stick**
  - Ensure **Bluetooth** and (if needed) **Koetori** permission in Settings → Privacy.
  - Ensure the app uses the same service UUID `4fafc201-1fb5-459e-8fcc-c5c9c331914b` and characteristic UUIDs as in `config.h`.

## 6. Protocol summary (for iOS app)

- **Service**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- **Audio** `…26a8`: notify, 512 bytes = `[chunk_index_lo, chunk_index_hi] + 510 bytes PCM` (16 kHz, 16‑bit mono, skip 44‑byte WAV header on stick).
- **Control** `…26a9**: M5 → app: `START:<ts>:16000:1`, `END:<total_chunks>`, `ERROR:message`. App → M5: `SUCCESS:category:confidence`.
- **Status** `…26aa**: optional, e.g. `B:<battery> R:<recording 0|1> Q:<queue_count>`.

The stick sends raw PCM after the WAV header; the app can prepend a 44‑byte WAV header and send that to the Koetori API.
