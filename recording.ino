void startRecording() {
  Serial.println("\n=== STARTING ===");
  
  // Recording works offline - queue system will handle upload later
  // User can connect WiFi manually via menu (B button when disconnected)
  
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

void saveAndUpload() {
  // For long mode, file is already written to SPIFFS; skip to upload
  if (isLongRecordingMode) {
    Serial.println("Long mode: File already saved to SPIFFS");
  } else {
    // NORMAL MODE: Write from RAM buffer to file
    Serial.println("Writing to file...");
    
    // Now write to file
    if (SPIFFS.exists("/rec.wav")) {
      SPIFFS.remove("/rec.wav");
    }
    
    File audioFile = SPIFFS.open("/rec.wav", FILE_WRITE);
    if (!audioFile) {
      displayError("File Error");
      delay(2000);
      displayReady();
      return;
    }
    
    // Write WAV header
    uint32_t dataSize = samplesRecorded * 2;
    writeWAVHeader(audioFile, dataSize);
    
    // Apply gain and write audio data in chunks WITH PROGRESS
    Serial.printf("Writing %d samples...\n", samplesRecorded);
    resetScreenPower();  // Start brightness cycle
    for (uint32_t i = 0; i < samplesRecorded; i++) {
      // Update progress every 5000 samples
      if (i % 5000 == 0) {
        int percent = (i * 100) / samplesRecorded;
        manageScreenPower();  // Handle dimming/off
        
        M5.Display.fillScreen(COLOR_BG_PRIMARY);
        
        // "SAVING" - smaller text, left margin
        drawTextSafe("SAVING", 5, 30, 3, COLOR_WHITE);
        
        // Progress bar at bottom
        drawProgress(percent);
        
        Serial.printf("Save progress: %d%%\n", percent);
      }
      
      int32_t sample = recordingBuffer[i] * 10;  // 10x gain
      if (sample > 32767) sample = 32767;
      if (sample < -32768) sample = -32768;
      int16_t amplified = (int16_t)sample;
      audioFile.write((uint8_t*)&amplified, 2);
    }
    
    audioFile.close();
    Serial.println("File saved");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    bool success = uploadRecording();
    
    if (success) {
      // Parse API response
      UploadResponse apiResp = parseUploadResponse(lastApiResponse);
      
      // Show success with enhanced info
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      drawTextSafe("SENT", 10, 30, 4, COLOR_GREEN);
      
      M5.Display.setTextSize(1);
      M5.Display.setTextColor(COLOR_WHITE);
      
      int y = 80;
      
      // Category (if available)
      if (apiResp.category.length() > 0) {
        M5.Display.setCursor(10, y);
        M5.Display.printf("Type: %s", apiResp.category.c_str());
        y += 12;
      }
      
      // Recording info (use API data if available, else calculate)
      if (apiResp.durationSeconds > 0 || apiResp.fileSizeKb > 0) {
        M5.Display.setCursor(10, y);
        if (apiResp.durationSeconds > 0 && apiResp.fileSizeKb > 0) {
          M5.Display.printf("%.1fs / %dKB", apiResp.durationSeconds, apiResp.fileSizeKb);
        } else if (apiResp.fileSizeKb > 0) {
          M5.Display.printf("%dKB", apiResp.fileSizeKb);
        }
        y += 12;
      } else {
        // Fallback: calculate from file
        File f = SPIFFS.open("/rec.wav");
        if (f) {
          size_t fileSize = f.size();
          f.close();
          float durationSec = (fileSize - 44) / 32000.0;
          M5.Display.setCursor(10, y);
          M5.Display.printf("%.1fs / %dKB", durationSec, fileSize / 1024);
          y += 12;
        }
      }
      
      // Tokens used (if available)
      if (apiResp.tokensUsed > 0) {
        M5.Display.setCursor(10, y);
        M5.Display.printf("Tokens: %d", apiResp.tokensUsed);
        y += 12;
      }
      
      // Quota remaining (if available)
      if (apiResp.tokensRemaining > 0 || apiResp.quotaPercent > 0) {
        M5.Display.setCursor(10, y);
        if (apiResp.tokensRemaining > 0) {
          M5.Display.printf("Quota: %d left", apiResp.tokensRemaining);
        } else if (apiResp.quotaPercent > 0) {
          M5.Display.printf("Quota: %d%% used", apiResp.quotaPercent);
        }
        y += 12;
      }
      
      // Processing time (if available)
      if (apiResp.processingTimeMs > 0) {
        M5.Display.setCursor(10, y);
        M5.Display.printf("Time: %.1fs", apiResp.processingTimeMs / 1000.0);
      }
      
      delay(2500);  // Longer delay to show stats
      
      // Clean up
      SPIFFS.remove("/rec.wav");
      hasRecording = false;
      
      // Keep WiFi connected with sleep mode (already enabled in connectWiFi)
      Serial.println("WiFi staying connected (sleep mode)");
      
      lastInteractionTime = millis();
      displayReady();
    } else {
      // Failed - add to queue
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
    // No WiFi - add to queue
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
}
