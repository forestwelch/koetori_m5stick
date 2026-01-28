# Koetori BLE Audio Streaming – Implementation Spec

This document describes the current M5 Stick ↔ iOS BLE audio streaming implementation so we can debug chunk loss and overlapping streams. Use it to brainstorm fixes (e.g. throttling, backpressure, or protocol changes).

---

## 1. Overview

- **M5 Stick (ESP32)** records 16 kHz mono 16‑bit PCM, then streams it to the **iPhone** over BLE.
- **iPhone** reassembles chunks into a WAV file, uploads to the Koetori API, then writes `SUCCESS:category:confidence` back to the M5 on the Control characteristic.
- Two streaming paths on M5:
  - **RAM path (fast):** `streamViaBLEFromSamples(buffer, count)` — no SPIFFS, used when BLE connected and normal-mode recording.
  - **File path:** `streamViaBLE("/rec.wav")` — reads from SPIFFS (long recordings or queue sync).

---

## 2. BLE Service and Characteristics

| UUID                                   | Name    | Properties          | Role                                            |
| -------------------------------------- | ------- | ------------------- | ----------------------------------------------- |
| `4fafc201-1fb5-459e-8fcc-c5c9c331914b` | Service | —                   | Koetori service                                 |
| `beb5483e-36e1-4688-b7f5-ea07361b26a8` | Audio   | Read, Notify        | Audio chunk stream (payloads only)              |
| `beb5483e-36e1-4688-b7f5-ea07361b26a9` | Control | Read, Write, Notify | START/END/ERROR from M5; SUCCESS from iPhone    |
| `beb5483e-36e1-4688-b7f5-ea07361b26aa` | Status  | Read, Notify        | Battery, recording, queue (e.g. `B:85 R:0 Q:0`) |

- **Device name:** `Koetori-M5-<DEVICE_ID>` (advertised; iOS discovers by service UUID and name prefix).

---

## 3. Protocol: Control Messages (M5 → iPhone)

All sent as UTF‑8 strings on the **Control** characteristic via **notify**.

| Message                     | When                 | Meaning                                                                        |
| --------------------------- | -------------------- | ------------------------------------------------------------------------------ |
| `START:<ts>:<sampleRate>:1` | Start of stream      | New transfer; `<ts>` = `millis()`, `<sampleRate>` = 16000, last `:1` reserved. |
| `END:<N>`                   | After last chunk     | Transfer complete; `<N>` = total chunk count (decimal).                        |
| `ERROR:file_open`           | File path failed     | Could not open file (file path only).                                          |
| `ERROR:file_too_small`      | File &lt;= 44 bytes  | —                                                                              |
| `ERROR:no_data`             | RAM path, no samples | —                                                                              |

**iPhone → M5 (Write on Control):**

| Message                           | When                    | Meaning                                             |
| --------------------------------- | ----------------------- | --------------------------------------------------- |
| `SUCCESS:<category>:<confidence>` | After successful upload | e.g. `SUCCESS:other:0.95`. M5 shows this on screen. |

---

## 4. Audio Chunk Format (M5 → iPhone, on Audio characteristic)

Each **notification** on the Audio characteristic is one chunk:

| Offset | Size  | Content                                                           |
| ------ | ----- | ----------------------------------------------------------------- |
| 0–1    | 2     | Chunk index, **little‑endian** `uint16_t` (0, 1, 2, …)            |
| 2–511  | 0–510 | Raw 16‑bit mono PCM, **little‑endian**, 255 samples max per chunk |

- **Constants (M5 `config.h`):**  
  `BLE_AUDIO_CHUNK_SIZE = 512`, `BLE_AUDIO_PAYLOAD_SIZE = 510`.
- **RAM path:** 255 samples/chunk (510 bytes payload), 10× gain applied before send.
- **File path:** up to 510 bytes per chunk (already 10× in file), last chunk may be shorter.
- **No WAV header in the stream** — iPhone builds a 44‑byte header and prepends it when saving the WAV.

---

## 5. M5 Sending Behavior

### 5.1 Common logic (both paths)

1. Send `START:<ts>:16000:1` on Control (notify).
2. For each chunk: build `[index_low, index_high, …payload…]`, `setValue` on Audio, `notify()`, then `delay(10)` (**10 ms** between notifications).
3. After all chunks, send `END:<totalChunks>` on Control.

### 5.2 Rates and sizes

- **Throughput:** 1 chunk every 10 ms ⇒ **100 chunks/s** nominal.
- **Payload:** 510 bytes/chunk ⇒ **51 kB/s**.
- **Length examples:**
  - 5 s @ 16 kHz ⇒ 80,000 samples ⇒ 80,000 / 255 ≈ **314 chunks** ⇒ ~3.1 s send time.
  - 20 s ⇒ **~1255 chunks** ⇒ ~12.5 s send time.
  - 30 s (normal max) ⇒ **~1883 chunks** ⇒ ~18.8 s send time.
  - 42 s (long max) ⇒ **~2636 chunks** ⇒ ~26.4 s send time.

### 5.3 When each path is used

- **RAM path:** `isBLEConnected() && !isLongRecordingMode` → `streamViaBLEFromSamples(recordingBuffer, samplesRecorded)`; no SPIFFS write.
- **File path:** long recording (file already on SPIFFS) or queue sync → `streamViaBLE("/rec.wav")`.
- Only one stream is sent per user “stop” (or per queue file); the next stream starts only after the previous `streamViaBLE*` returns (i.e. after END is sent). **Exception:** when sending the queue, the M5 sends files one after another with no wait for iPhone; so a new `START` can reach the app while it is still handling the previous transfer (assembling/uploading).

---

## 6. iOS Receive and Assembly

### 6.1 Threading and storage

- **ChunkStorage:** a small, **thread‑safe** cache used from the BLE callback.
  - `chunks: [UInt16: Data]` — index → payload (index from first 2 bytes of notification).
  - `expectedCount: Int?` — set when `END:<N>` is received.
  - **Audio notifications** are handled in the **CoreBluetooth callback** (main queue if Central is created with `queue: nil`). The handler does **synchronous** work only: copy `characteristic.value` into `Data`, then `chunkStorage.addChunk(dataCopy)`. No `Task { }` or async in that path to avoid flooding the main queue.
- **Control notifications** are dispatched to `@MainActor` via `Task { @MainActor in self.handleControlMessage(msg) }`.

### 6.2 Control handling

- **START:**
  - `cancelReceive()` (clear timeouts, `chunkStorage.reset(sampleRate)`).
  - Set `connectionState = .receiving(name)`.
  - Parse sample rate from `START:` and pass to `reset(sampleRate:)`.
  - `scheduleReceiveTimeout()` — **10 s**; on fire → “Recording timed out (no data)” and cancel.
- **END:**
  - Cancel receive timeout.
  - Parse `total`, `chunkStorage.setExpected(total)`.
  - `tryAssembleAndNotify()`.
  - If `receivedCount() < total` → `scheduleStragglerWait(expected: total)` — **2 s**; on fire → `checkStragglersAndAssemble(expected:)`.
  - In that callback: if still `have < expected` → show error “still missing N chunks after 2s wait”, `cancelReceive()`.
- **ERROR:**
  - `cancelReceive()`, show “M5 error: …”.

### 6.3 Assembly and upload

- **takeIfComplete():**
  - Requires `chunks.count >= expected` and every index `0 ..< expected` present.
  - Builds ordered PCM by concatenating `chunks[0]`, `chunks[1]`, …; clears storage; returns `(pcm, sampleRate)`.
- **tryAssembleAndNotify():**
  - Calls `takeIfComplete()`. If non‑nil: build WAV = `Data.wavHeader(dataSize:rate:) + pcm`, write to temp file, `cancelReceive()`, call `onAudioAssembled?(fileURL)`.
- **RecordingView** sets `onAudioAssembled = { url in Task { await uploadBLEAudio(fileURL: url) } }`. After upload, the app can call `writeToControl("SUCCESS:category:confidence")` so the M5 can show the result.

### 6.4 Timeouts and constants (BLEManager)

- `receiveTimeoutSeconds = 10` — global receive timeout after START.
- `stragglerWaitSeconds = 2` — extra wait after END when chunks are missing.
- `audioChunkPayloadSize = 510` — matches M5 (used only conceptually; assembly uses “all bytes after index” as payload).

---

## 7. WAV Output (iOS)

- **Data+WAV.swift:** `Data.wavHeader(dataSize: sampleRate:)` returns 44 bytes (RIFF, fmt, data chunk headers) for 16‑bit mono at the given sample rate.
- Assembled file = header + concatenated payloads in index order. No re‑encoding; payloads are already little‑endian 16‑bit PCM.

---

## 8. Observed Failure Modes (from your logs)

### 8.1 Heavy chunk loss on long streams

- **Example:** `END received, have 468/1417 chunks` → ~67% of chunks missing.
- **Implication:** For a ~22.6 s recording (1417×255 samples), the M5 sends for ~14.2 s at 100 chunks/s. iOS only logged 468 chunks, so a large fraction of notifications are never applied (or a new START replaced them — see below).
- **Likely causes:**
  - CoreBluetooth / iOS dropping notifications when they arrive at ~100/s for many seconds (internal buffers or queue depth).
  - No backpressure: M5 sends at a fixed 10 ms rate regardless of iPhone load.

### 8.2 Overlapping START (session reset)

- **Example:** Chunk count goes 700 → 1, then `Control START -> expecting audio chunks` and counts 1, 50, 100…
- **Meaning:** A new `START` was processed while the previous run was still in progress. `handleControlMessage("START:...")` calls `cancelReceive()` and `chunkStorage.reset()`, so all previous chunks are discarded and a new session starts.
- **Possible causes:**
  - **Queue sync:** M5 `sendQueue()` calls `streamViaBLE()` for each queued file in a loop. After the first file’s END, it immediately sends the next file’s START. If the iPhone is still in “receiving” (assembling or uploading), the next START resets the session and the first run never completes.
  - **User starting a new recording on the M5** while the app is still receiving (unlikely if the M5 blocks in `saveAndUpload` until the stream is done).
  - **Duplicate or retransmitted START** from stack/radio (less likely).

### 8.3 Chunk index type on iOS

- Indices are stored as `UInt16`. Valid chunk counts are 0–65535.
- 30 s @ 255 samples/chunk ⇒ ~1883 chunks; 42 s ⇒ ~2636. Both are well below 65535, so **wrap‑around is not** the cause of the 468/1417 failure.

---

## 9. File / Symbol Reference

**M5 (koetori_m5stick):**

- `config.h` — `SAMPLE_RATE`, `BLE_*`, UUIDs, chunk sizes.
- `ble_handler.h` / `ble_handler.ino` — `streamViaBLE()`, `streamViaBLEFromSamples()`, `sendControl()`, connection state.
- `recording.ino` — `saveAndUpload()`, `streamFromRam` branch, `writeRecordingToFile()`, `sendQueue()`.

**iOS (Koetori):**

- `BLEManager.swift` — service/characteristic UUIDs, `ChunkStorage`, control handling, timeouts, `onAudioAssembled`.
- `Data+WAV.swift` — `wavHeader(dataSize:sampleRate:)`.
- `RecordingView.swift` — `onAudioAssembled` → `uploadBLEAudio`, BLE status UI.

---

## 10. Design Choices Worth Revisiting

1. **Fixed 10 ms delay on M5** — Good for reducing drops vs. “as fast as possible,” but 100 chunks/s may still be too high for iOS on long streams. Options: increase to 15–20 ms, or add an explicit “ready for next chunk” from iPhone (backpressure).
2. **No ACK per chunk** — Protocol is fire‑and‑forget. Either throttle conservatively or add optional ACK/sequence and retransmit.
3. **START resets everything** — Any new START (e.g. next file in queue) wipes the current run. Options: sequence numbers / session IDs so the app can ignore late or duplicate STARTs, or have the M5 wait for an explicit “ready for next” from the app before sending the next file.
4. **Single ChunkStorage** — One in‑flight transfer at a time. Fits current model; if you later support multiple sessions, you’d need keying by session id.
5. **iOS callback queue** — Central is created with `queue: nil` (main). Audio path avoids MainActor in the hot path; Control path hops to MainActor. If you see main-thread pressure, consider a dedicated serial queue for the Central and then marshalling to MainActor only where needed.

Use this spec to align M5 and iOS behavior and to try slower send rates, backpressure, or queue sync flow changes (e.g. “send next file only after SUCCESS or N seconds”) to reduce loss and overlapping STARTs.
