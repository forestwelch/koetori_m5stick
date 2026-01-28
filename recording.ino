#include "ble_handler.h"

void startRecording() {
  Serial.println("\n=== STARTING ===");
  
  // Recording works offline - queue system will sync when BLE connected
  // User can open Koetori iOS app and connect to this device
  
  samplesRecorded = 0;
  recordingStartTime = millis();
  resetScreenPower();  // Start brightness cycle
  
  // Get recording parameters based on mode
  uint32_t recordingTimeMax = isLongRecordingMode ? RECORDING_TIME_LONG : RECORDING_TIME_NORMAL;
  uint32_t maxSamples = SAMPLE_RATE * recordingTimeMax;
  
  Serial.printf("Recording mode: %s (%ds max)\n", 
    isLongRecordingMode ? "LONG" : "NORMAL", 
    recordingTimeMax);
  
  // For long recordings, stream to SPIFFS; for normal, use RAM buffer
  File streamFile;
  if (isLongRecordingMode) {
    // Clean up any leftover stream file from previous failed recording
    if (SPIFFS.exists("/rec_stream.wav")) {
      Serial.println("Removing old stream file...");
      SPIFFS.remove("/rec_stream.wav");
    }
    
    // Check if we have enough space (with 5% safety margin)
    size_t requiredSpace = (size_t)SAMPLE_RATE * 2 * recordingTimeMax;
    size_t availableSpace = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    size_t requiredWithMargin = requiredSpace + (requiredSpace / 20);  // 5% margin
    Serial.printf("Required: %d bytes (with margin: %d), Available: %d bytes\n", 
                  requiredSpace, requiredWithMargin, availableSpace);
    
    if (availableSpace < requiredWithMargin) {
      Serial.println("ERROR: Not enough SPIFFS space");
      displayError("Storage Full");
      delay(2000);
      displayReady();
      return;
    }
    
    Serial.println("Opening stream file...");
    streamFile = SPIFFS.open("/rec_stream.wav", FILE_WRITE);
    if (!streamFile) {
      Serial.println("ERROR: Failed to open stream file");
      displayError("File Error");
      delay(2000);
      displayReady();
      return;
    }
    // Write placeholder WAV header (will update later)
    writeWAVHeader(streamFile, 0);
  }
  
  Serial.println("Recording in tight loop...");
  
  // PROPER EVENT-DRIVEN RECORDING
  static constexpr size_t buffer_size = 240;
  int16_t tempBuffer[buffer_size];
  uint32_t lastDisplayUpdate = 0;
  
  while (samplesRecorded < maxSamples) {
    // This returns TRUE when buffer_size samples are ready!
    if (M5.Mic.record(tempBuffer, buffer_size, SAMPLE_RATE)) {
      // Data is ready!
      if (samplesRecorded + buffer_size <= maxSamples) {
        if (isLongRecordingMode) {
          // LONG MODE: Stream to SPIFFS with gain applied
          bool writeError = false;
          for (size_t i = 0; i < buffer_size; i++) {
            int32_t sample = tempBuffer[i] * 10;  // 10x gain
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            int16_t amplified = (int16_t)sample;
            size_t written = streamFile.write((uint8_t*)&amplified, 2);
            if (written != 2) {
              writeError = true;
              break;
            }
          }
          
          // If write failed (likely SPIFFS full), abort recording
          if (writeError) {
            Serial.println("ERROR: Write failed - SPIFFS likely full");
            streamFile.close();
            SPIFFS.remove("/rec_stream.wav");
            displayError("Storage Full");
            delay(2000);
            displayReady();
            return;
          }
        } else {
          // NORMAL MODE: Buffer in RAM
          if (samplesRecorded + buffer_size <= MAX_RECORDING_SIZE / 2) {
            memcpy(&recordingBuffer[samplesRecorded], tempBuffer, buffer_size * 2);
          }
        }
        samplesRecorded += buffer_size;
        
        // Manage screen power (dim after 1s, off after 3s) - no button check
        manageScreenPowerNoButton();
        
        // Update display every 100ms
        if (millis() - lastDisplayUpdate > 100) {
          lastDisplayUpdate = millis();
          
          M5.Display.fillScreen(COLOR_BG_PRIMARY);
          
          // Recording progress bar at top (thin, discreet)
          uint32_t elapsed = millis() - recordingStartTime;
          int progressPercent = (elapsed * 100) / (recordingTimeMax * 1000);
          if (progressPercent > 100) progressPercent = 100;
          
          int barWidth = 135;  // Full screen width
          int filledWidth = (barWidth * progressPercent) / 100;
          
          // Draw filled portion in red
          if (filledWidth > 0) {
            M5.Display.fillRect(0, 0, filledWidth, 3, COLOR_RED);
          }
          // Draw unfilled portion in dark gray
          if (filledWidth < barWidth) {
            M5.Display.fillRect(filledWidth, 0, barWidth - filledWidth, 3, 0x2104);
          }
          
          // "REC" in red, left aligned
          drawTextSafe("REC", 10, 50, 4, COLOR_RED);
          
          // Show time elapsed (for long recordings)
          if (isLongRecordingMode) {
            M5.Display.setTextSize(1);
            M5.Display.setCursor(10, 90);
            M5.Display.setTextColor(COLOR_WHITE);
            M5.Display.printf("%d/%ds", (int)(elapsed / 1000), recordingTimeMax);
          }
          
          // Button hints: A = stop, B = cancel (stacked at bottom)
          drawButtonHint("STOP", COLOR_GREEN, 210, "A");
          drawButtonHint("CANCEL", COLOR_RED, 225, "B");
        }
        
        // Progress logging
        if (samplesRecorded % 16000 < buffer_size) {
          Serial.printf("%.1fs (%d samples)\n", (float)samplesRecorded / SAMPLE_RATE, samplesRecorded);
        }
      }
    }
    
    // Check for buttons
    M5.update();
    if (M5.BtnA.wasPressed()) {
      Serial.println("A pressed - stopping");
      resetScreenPower();  // Reset brightness for next screen
      break;
    }
    if (M5.BtnB.wasPressed()) {
      Serial.println("B pressed - canceling");
      resetScreenPower();  // Reset brightness for next screen
      hasRecording = false;
      samplesRecorded = 0;
      if (isLongRecordingMode && streamFile) {
        streamFile.close();
        SPIFFS.remove("/rec_stream.wav");
      }
      cancelRecording();
      // Consume button state to prevent main loop from seeing it
      delay(200);
      M5.update();
      return;  // Exit immediately, don't proceed to review
    }
    
    // Check time limit
    uint32_t elapsed = (millis() - recordingStartTime) / 1000;
    if (elapsed >= recordingTimeMax) {
      Serial.println("Time limit");
      resetScreenPower();  // Reset brightness for next screen
      break;
    }
    
    delay(1);  // Small delay to prevent watchdog issues
  }
  
  // Close stream file if in long mode
  if (isLongRecordingMode && streamFile) {
    // Update WAV header with actual size
    uint32_t dataSize = samplesRecorded * 2;
    streamFile.seek(0);
    writeWAVHeader(streamFile, dataSize);
    streamFile.close();
    Serial.println("Stream file closed and finalized");
    
    // Rename to rec.wav for compatibility
    SPIFFS.remove("/rec.wav");
    SPIFFS.rename("/rec_stream.wav", "/rec.wav");
  }
  
  Serial.printf("Recording done: %d samples\n", samplesRecorded);
  hasRecording = true;  // Mark that we have audio to send or cancel
  stopRecording();
}

void cancelRecording() {
  Serial.println("\n=== CANCEL ===");
  
  // Clear the recording
  hasRecording = false;
  samplesRecorded = 0;
  
  lastInteractionTime = millis();  // Reset idle timer
  displayReady();
}

void stopRecording() {
  Serial.println("\n=== STOPPING ===");
  isRecording = false;
  
  // Show "PROCESSING..." feedback
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  drawTextSafe("PROCESSING", 10, 60, 2, COLOR_WHITE);
  
  delay(100);
  
  Serial.printf("Recorded %d samples (%d bytes)\n", samplesRecorded, samplesRecorded * 2);
  
  // Validate sample count based on mode
  uint32_t maxSamples = isLongRecordingMode ? 
    (SAMPLE_RATE * RECORDING_TIME_LONG) : 
    (MAX_RECORDING_SIZE / 2);
  
  if (samplesRecorded == 0 || samplesRecorded > maxSamples) {
    Serial.printf("ERROR: Invalid sample count: %d\n", samplesRecorded);
    displayError("Bad Recording");
    delay(2000);
    displayReady();
    return;
  }
  
  // Go straight to save and upload (skip review screen)
  saveAndUpload();
}

void displayReview() {
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  
  // "done" - left aligned, consistent
  drawTextSafe("DONE", 10, 50, 4, COLOR_WHITE);
  
  // Button hints: A = send, B = cancel (stacked at bottom)
  drawButtonHint("SEND", COLOR_GREEN, 210, "A");
  drawButtonHint("CANCEL", COLOR_RED, 225, "B");
  
  Serial.println("Review: A=send, B=cancel");
  
  // Wait for user input
  unsigned long waitStart = millis();
  while (millis() - waitStart < 30000) {  // 30 second timeout
    M5.update();
    
    if (M5.BtnA.wasPressed()) {
      Serial.println("User chose: send");
      saveAndUpload();
      return;
    }
    
    if (M5.BtnB.wasPressed()) {
      Serial.println("User chose: cancel");
      cancelRecording();
      return;
    }
    
    delay(100);
  }
  
  // Timeout - auto upload
  Serial.println("Timeout - auto sending");
  saveAndUpload();
}

// Write current RAM buffer to /rec.wav (used when queueing or when BLE stream failed).
static bool writeRecordingToFile() {
  if (SPIFFS.exists("/rec.wav")) SPIFFS.remove("/rec.wav");
  File audioFile = SPIFFS.open("/rec.wav", FILE_WRITE);
  if (!audioFile) return false;
  uint32_t dataSize = samplesRecorded * 2;
  writeWAVHeader(audioFile, dataSize);
  for (uint32_t i = 0; i < samplesRecorded; i++) {
    int32_t sample = recordingBuffer[i] * 10;
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    int16_t amplified = (int16_t)sample;
    audioFile.write((uint8_t*)&amplified, 2);
  }
  audioFile.close();
  return true;
}

void saveAndUpload() {
  const bool bleConnected = isBLEConnected();
  const bool streamFromRam = bleConnected && !isLongRecordingMode;
  
  Serial.printf("saveAndUpload: BLE=%d, longMode=%d, streamFromRam=%d\n", 
                bleConnected ? 1 : 0, isLongRecordingMode ? 1 : 0, streamFromRam ? 1 : 0);

  // Only write to SPIFFS when we're not using the RAM-stream fast path.
  if (!streamFromRam) {
    if (isLongRecordingMode) {
      Serial.println("Long mode: File already saved to SPIFFS");
    } else {
      Serial.println("Writing to file...");
      if (SPIFFS.exists("/rec.wav")) SPIFFS.remove("/rec.wav");
      File audioFile = SPIFFS.open("/rec.wav", FILE_WRITE);
      if (!audioFile) {
        displayError("File Error");
        delay(2000);
        displayReady();
        return;
      }
      uint32_t dataSize = samplesRecorded * 2;
      writeWAVHeader(audioFile, dataSize);
      Serial.printf("Writing %d samples...\n", samplesRecorded);
      resetScreenPower();
      for (uint32_t i = 0; i < samplesRecorded; i++) {
        if (i % 5000 == 0) {
          int percent = (i * 100) / samplesRecorded;
          manageScreenPower();
          M5.Display.fillScreen(COLOR_BG_PRIMARY);
          drawTextSafe("SAVING", 5, 30, 3, COLOR_WHITE);
          drawProgress(percent);
          Serial.printf("Save progress: %d%%\n", percent);
        }
        int32_t sample = recordingBuffer[i] * 10;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        int16_t amplified = (int16_t)sample;
        audioFile.write((uint8_t*)&amplified, 2);
      }
      audioFile.close();
      Serial.println("File saved");
    }
  }

  if (isBLEConnected()) {
    M5.Display.fillScreen(COLOR_BG_PRIMARY);
    drawTextSafe("SENDING", 10, 60, 2, COLOR_WHITE);
    delay(300);

    lastApiResponse = "";
    bool success = streamFromRam
      ? streamViaBLEFromSamples(recordingBuffer, samplesRecorded)
      : streamViaBLE("/rec.wav");

    if (success) {
      UploadResponse apiResp;
      apiResp.success = true;
      apiResp.category = "";
      apiResp.confidence = 0;
      if (lastApiResponse.startsWith("SUCCESS:")) {
        int i = 8;
        int colon = lastApiResponse.indexOf(':', i);
        if (colon > i) {
          apiResp.category = lastApiResponse.substring(i, colon);
          apiResp.confidence = lastApiResponse.substring(colon + 1).toFloat();
        }
      }

      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      drawTextSafe("SENT", 10, 30, 4, COLOR_GREEN);

      M5.Display.setTextSize(1);
      M5.Display.setTextColor(COLOR_WHITE);
      int y = 80;
      if (apiResp.category.length() > 0) {
        M5.Display.setCursor(10, y);
        if (apiResp.confidence > 0) {
          M5.Display.printf("%s (%.0f%%)", apiResp.category.c_str(), apiResp.confidence * 100);
        } else {
          M5.Display.printf("%s", apiResp.category.c_str());
        }
        y += 12;
      }
      if (streamFromRam) {
        float durationSec = (float)samplesRecorded / (float)SAMPLE_RATE;
        uint32_t sizeBytes = samplesRecorded * 2 + 44;
        M5.Display.setCursor(10, y);
        M5.Display.printf("%.1fs / %dKB", durationSec, (int)(sizeBytes / 1024));
      } else {
        File f = SPIFFS.open("/rec.wav");
        if (f) {
          size_t fileSize = f.size();
          f.close();
          float durationSec = (fileSize - 44) / 32000.0;
          M5.Display.setCursor(10, y);
          M5.Display.printf("%.1fs / %dKB", durationSec, (int)(fileSize / 1024));
        }
      }

      delay(2500);
      if (!streamFromRam) SPIFFS.remove("/rec.wav");
      hasRecording = false;
      lastInteractionTime = millis();
      displayReady();
    } else {
      if (streamFromRam) {
        Serial.println("BLE send failed, writing to file and queue...");
        if (!writeRecordingToFile()) {
          displayError("File Error");
          delay(2000);
          displayReady();
          return;
        }
      }
      if (addToQueue()) {
        M5.Display.fillScreen(COLOR_BG_PRIMARY);
        drawTextSafe("SAVED", 10, 40, 4, COLOR_WHITE);
        drawTextSafe("B: Retry", 10, 80, 2, COLOR_WHITE);
        delay(2000);
      } else {
        displayError("Queue Full");
        delay(2000);
      }
      hasRecording = false;
      displayReady();
    }
  } else {
    if (addToQueue()) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      drawTextSafe("SAVED", 10, 40, 4, COLOR_WHITE);
      drawTextSafe("Open app to sync", 10, 80, 1, COLOR_WHITE);
      delay(2000);
    } else {
      displayError("Queue Full");
      delay(2000);
    }
    hasRecording = false;
    displayReady();
  }
}

void sendQueue() {
  int sent = 0;
  int failed = 0;

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  char queueFiles[5][48];
  int fileCount = 0;

  while (file && fileCount < 5) {
    String filename = String(file.name());
    if (filename.startsWith("/q_") && filename.endsWith(".wav")) {
      strcpy(queueFiles[fileCount], filename.c_str());
      fileCount++;
      Serial.printf("Found queue file: %s\n", filename.c_str());
    }
    file = root.openNextFile();
  }

  if (fileCount != queueCount) {
    queueCount = fileCount;
    saveQueueCount(queueCount);
  }

  if (fileCount == 0) {
    M5.Display.fillScreen(COLOR_BG_PRIMARY);
    drawTextSafe("QUEUE", 10, 40, 2, COLOR_GRAY);
    drawTextSafe("EMPTY", 10, 65, 2, COLOR_GRAY);
    delay(1500);
    return;
  }

  if (!isBLEConnected()) {
    M5.Display.fillScreen(COLOR_BG_PRIMARY);
    drawTextSafe("CONNECT", 10, 40, 2, COLOR_YELLOW);
    drawTextSafe("Open Koetori app", 10, 70, 1, COLOR_WHITE);
    delay(2000);
    return;
  }

  for (int i = 0; i < fileCount; i++) {
    Serial.printf("Sending %s via BLE...\n", queueFiles[i]);
    M5.Display.fillScreen(COLOR_BG_PRIMARY);
    char progressText[32];
    sprintf(progressText, "SEND %d/%d", i + 1, fileCount);
    drawTextSafe(progressText, 10, 50, 2, COLOR_WHITE);

    bool success = streamViaBLE(queueFiles[i]);
    if (success) {
      SPIFFS.remove(queueFiles[i]);
      sent++;
    } else {
      failed++;
    }
    // 3s gap so iPhone can finish assembly/upload before next START (avoids overlapping START)
    delay(3000);
  }

  queueCount = failed;
  saveQueueCount(queueCount);

  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  char resultText[32];
  if (sent > 0 && failed == 0) {
    sprintf(resultText, "SENT %d", sent);
    drawTextSafe(resultText, 10, 50, 2, COLOR_GREEN);
  } else if (sent > 0 && failed > 0) {
    sprintf(resultText, "OK: %d", sent);
    drawTextSafe(resultText, 10, 40, 2, COLOR_GREEN);
    sprintf(resultText, "FAIL: %d", failed);
    drawTextSafe(resultText, 10, 65, 2, COLOR_RED);
  } else {
    sprintf(resultText, "FAIL %d", failed);
    drawTextSafe(resultText, 10, 50, 2, COLOR_RED);
  }
  delay(2000);
  Serial.printf("Queue complete: %d sent, %d failed\n", sent, failed);
}

void loop() {
  M5.update();

  // Auto-send queue when BLE connects and we have queued files (no menu needed)
  if (isBLEConnected() && queueCount > 0 && !isRecording && !hasRecording) {
    sendQueue();
    lastInteractionTime = millis();
    // sendQueue() blocks and returns when done; loop continues
  }

  // Manage screen power (dim quickly, then off)
  if (!isRecording && !hasRecording && !screenIsOff) {
    uint32_t idleTime = millis() - lastInteractionTime;
    if (idleTime >= SCREEN_DIM_DELAY && !screenIsDimmed) {
      M5.Display.setBrightness(SCREEN_BRIGHTNESS_DIMMED);
      screenIsDimmed = true;
      Serial.println("Ready screen: DIMMED");
    }
    if (idleTime >= IDLE_SCREEN_OFF_DELAY) {
      screenOff();
    }
  }

  // Button A: Record / Stop. One press = wake (if off) and/or start recording; no "load" tap.
  static unsigned long lastPressA = 0;
  if (M5.BtnA.wasPressed() && (millis() - lastPressA > 500)) {
    lastPressA = millis();
    lastInteractionTime = millis();
    if (screenIsOff) {
      screenOn();
      // Same press starts recording so user doesn't have to tap twice
      resetScreenPower();
      startRecording();
    } else if (!isRecording) {
      resetScreenPower();
      startRecording();
    }
  }

  // B: wake screen; cancel recording if one is pending. No menu.
  static unsigned long lastPressB = 0;
  if (M5.BtnB.wasPressed() && (millis() - lastPressB > 500)) {
    lastPressB = millis();
    lastInteractionTime = millis();
    if (screenIsOff) screenOn();
    else if (hasRecording) cancelRecording();
  }
  // PWR: wake screen only
  if (M5.BtnPWR.wasPressed()) {
    lastInteractionTime = millis();
    if (screenIsOff) screenOn();
  }

  // Longer delay when idle = fewer CPU wakeups, better battery
  uint32_t idle = (!isRecording && !hasRecording) ? 250 : 100;
  delay(idle);
}

