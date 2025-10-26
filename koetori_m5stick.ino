/*
 * Koetori M5StickC PLUS2 - Voice Recording Device
 * 
 * Records audio using M5.Mic and uploads to Koetori API for transcription
 * and categorization.
 * 
 * See README.md for setup instructions.
 */

#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <esp_log.h>
#include "secrets.h"  // WiFi credentials, API keys, etc. (not committed to git)
#include "config.h"   // Configuration constants
#include "types.h"    // Type definitions
#include "globals.h"  // Global variable declarations
#include "sprites.h"  // Sprite drawing functions
#include "display.h"  // Display and text utility functions

// Global variable definitions
bool isRecording = false;
bool hasRecording = false;  // Track if we have audio to send or cancel
bool screenIsOff = false;     // Track if screen is actually off
bool screenIsDimmed = false;  // Track if screen is dimmed
uint32_t recordingStartTime = 0;
uint32_t lastInteractionTime = 0;
uint32_t screenStateStartTime = 0;  // Track screen state changes for dimming/off
int16_t* recordingBuffer = nullptr;
uint32_t samplesRecorded = 0;
int queueCount = 0;  // Number of queued recordings
int lastSuccessfulNetwork = -1;  // Remember which network worked last
uint32_t lastWiFiCheck = 0;  // For background reconnection
bool isLongRecordingMode = false;  // Toggle between normal (30s) and long (120s) recording
String lastApiResponse = "";  // Store last API response for parsing

// Forward declarations
bool uploadRecording(const char* filename = "/rec.wav");
void connectWiFiByIndex(int networkIndex, bool silent = false);

void setup() {
  // KILL ALL ERRORS IMMEDIATELY
  esp_log_level_set("*", ESP_LOG_NONE);  // Nuke everything
  
  // Reduce CPU frequency for power savings (default 240MHz -> 80MHz)
  setCpuFrequencyMhz(80);
  
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== Koetori M5StickC PLUS2 ===");
  Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());
  
  auto cfg = M5.config();
  cfg.internal_imu = false;
  M5.begin(cfg);
  
  M5.Display.setRotation(0);  // Portrait mode: 135x240
  M5.Display.setBrightness(SCREEN_BRIGHTNESS);
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  
  // Show Bailey sprite during boot (full screen 135x240)
  drawBailey();
  
  Serial.println("M5 initialized");
  
  // Initialize SPIFFS (quietly - don't auto-format to avoid watchdog spam)
  if (!SPIFFS.begin(false)) {
    // Only format if it fails
    SPIFFS.begin(true);
  }
  Serial.println("SPIFFS OK");
  
  // Load queue count and last network
  queueCount = loadQueueCount();
  lastSuccessfulNetwork = loadLastNetwork();
  Serial.printf("Queue: %d recordings\n", queueCount);
  Serial.printf("Last network: %d\n", lastSuccessfulNetwork);
  
  // Allocate recording buffer
  recordingBuffer = (int16_t*)malloc(MAX_RECORDING_SIZE);
  if (!recordingBuffer) {
    Serial.println("Failed to allocate buffer!");
    while(1) delay(1000);
  }
  Serial.println("Buffer allocated");
  
  // Initialize microphone with M5.Mic + noise filtering
  auto mic_cfg = M5.Mic.config();
  mic_cfg.noise_filter_level = 8;  // Higher = more noise filtering (0-255)
  M5.Mic.config(mic_cfg);
  
  if (M5.Mic.begin()) {
    Serial.println("Mic OK (with noise filter)");
  } else {
    Serial.println("Mic failed!");
  }
  
  // Try to auto-connect to last successful network
  if (lastSuccessfulNetwork >= 0 && lastSuccessfulNetwork < numNetworks) {
    Serial.printf("Attempting auto-connect to: %s\n", wifiNetworks[lastSuccessfulNetwork].ssid);
    connectWiFiByIndex(lastSuccessfulNetwork, true);  // Silent mode
  }
  
  // Show ready screen immediately
  lastInteractionTime = millis();  // Start idle timer
  displayReady();
  Serial.println("Setup complete\n");
}

// Connect to WiFi by network index (used for auto-connect)
void connectWiFiByIndex(int networkIndex, bool silent) {
  if (networkIndex < 0 || networkIndex >= numNetworks) {
    return;
  }
  
  WiFi.disconnect();
  WiFi.begin(wifiNetworks[networkIndex].ssid, wifiNetworks[networkIndex].password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {  // 5 second timeout
    if (!silent) {
      manageScreenPower();
    }
    delay(500);
    if (!silent) Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(true);  // Enable WiFi power save mode
    if (!silent) {
      Serial.println("\nAuto-connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    }
  } else {
    WiFi.disconnect();
    if (!silent) Serial.println("\nAuto-connect failed");
  }
}

void connectWiFi(bool silent = false) {
  if (!silent) {
    M5.Display.fillScreen(COLOR_BG_PRIMARY);
    
    // Show "WIFI" - left aligned, consistent
    M5.Display.setTextSize(4);
    M5.Display.setCursor(10, 50);
    M5.Display.setTextColor(COLOR_WHITE);
    M5.Display.println("WIFI");
    
    resetScreenPower();  // Start brightness cycle
  }
  
  // Try last successful network first (if we have one)
  if (lastSuccessfulNetwork >= 0 && lastSuccessfulNetwork < numNetworks) {
    Serial.printf("Trying last network: %s\n", wifiNetworks[lastSuccessfulNetwork].ssid);
    WiFi.begin(wifiNetworks[lastSuccessfulNetwork].ssid, wifiNetworks[lastSuccessfulNetwork].password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {  // 5 seconds timeout
      if (!silent) {
        manageScreenPower();
      }
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(true);  // Enable WiFi power save mode
      Serial.println("\nWiFi connected (last network)!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.println("WiFi sleep mode enabled");
      return;
    } else {
      WiFi.disconnect();
      Serial.println(" failed");
    }
  }
  
  // Try all networks with reduced timeout
  for (int i = 0; i < numNetworks; i++) {
    Serial.printf("Trying: %s\n", wifiNetworks[i].ssid);
    WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {  // 5 seconds timeout (reduced from 10)
      if (!silent) {
        manageScreenPower();
      }
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      lastSuccessfulNetwork = i;  // Remember this network
      saveLastNetwork(i);
      WiFi.setSleep(true);  // Enable WiFi power save mode
      Serial.println("\nWiFi connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.printf("Saved network %d as last successful\n", i);
      Serial.println("WiFi sleep mode enabled");
      return;
    } else {
      WiFi.disconnect();
    }
  }
  
  Serial.println("\nWiFi failed");
}

// Helper: Manage screen power state WITHOUT button checking (for active screens)
// Only dims, never turns off completely
void manageScreenPowerNoButton() {
  uint32_t elapsed = millis() - screenStateStartTime;
  
  if (elapsed >= SCREEN_DIM_DELAY && !screenIsDimmed) {
    // Dim after 2 seconds (set once per cycle)
    M5.Display.setBrightness(SCREEN_BRIGHTNESS_DIMMED);
    screenIsDimmed = true;
    Serial.println("Screen DIMMED");
  }
}

// Helper: Manage screen power state WITH button checking (for other screens)
// Dims after 2 seconds, A button resets brightness
void manageScreenPower() {
  // Check if A button pressed to wake screen
  M5.update();
  if (M5.BtnA.wasPressed()) {
    resetScreenPower();
    return;
  }
  
  // Call the no-button version to do the actual work
  manageScreenPowerNoButton();
}

// Helper: Reset screen to full brightness
void resetScreenPower() {
  if (screenIsOff) {
    M5.Display.wakeup();
    screenIsOff = false;
  }
  M5.Display.setBrightness(SCREEN_BRIGHTNESS);
  screenIsDimmed = false;
  screenStateStartTime = millis();
}

// Helper: Turn screen completely OFF (legacy)
void screenOff() {
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  screenIsOff = true;
  Serial.println("Screen OFF");
}

// Helper: Turn screen back ON (legacy)
void screenOn() {
  if (screenIsOff) {
    M5.Display.wakeup();
    M5.Display.setBrightness(SCREEN_BRIGHTNESS);
    screenIsOff = false;
    screenIsDimmed = false;
    lastInteractionTime = millis();  // Reset idle timer
    displayReady();  // Redraw the screen
    Serial.println("Screen ON");
  }
}

// Queue Management Functions

// Load queue count from SPIFFS
int loadQueueCount() {
  File file = SPIFFS.open("/queue_count.txt", FILE_READ);
  if (!file) return 0;
  
  int count = file.parseInt();
  file.close();
  return count;
}

// Save queue count to SPIFFS
void saveQueueCount(int count) {
  File file = SPIFFS.open("/queue_count.txt", FILE_WRITE);
  if (file) {
    file.println(count);
    file.close();
  }
}

// Load last successful network from SPIFFS
int loadLastNetwork() {
  File file = SPIFFS.open("/last_network.txt", FILE_READ);
  if (!file) return -1;
  
  int network = file.parseInt();
  file.close();
  return network;
}

// Save last successful network to SPIFFS
void saveLastNetwork(int networkIndex) {
  File file = SPIFFS.open("/last_network.txt", FILE_WRITE);
  if (file) {
    file.println(networkIndex);
    file.close();
  }
}

// Add current recording to queue
bool addToQueue() {
  if (queueCount >= 5) {  // Max 5 queued recordings
    Serial.println("Queue full!");
    return false;
  }
  
  // Create filename with timestamp
  char queueFilename[48];
  unsigned long timestamp = millis() / 1000;  // Seconds since boot
  sprintf(queueFilename, "/q_%lu_%d.wav", timestamp, queueCount);
  
  // Copy /rec.wav to queue
  File src = SPIFFS.open("/rec.wav", FILE_READ);
  File dst = SPIFFS.open(queueFilename, FILE_WRITE);
  
  if (!src || !dst) {
    Serial.println("Failed to queue recording");
    if (src) src.close();
    if (dst) dst.close();
    return false;
  }
  
  // Copy file
  uint8_t buf[512];
  while (src.available()) {
    size_t len = src.read(buf, sizeof(buf));
    dst.write(buf, len);
  }
  
  src.close();
  dst.close();
  
  queueCount++;
  saveQueueCount(queueCount);
  Serial.printf("Added to queue (%d total)\n", queueCount);
  return true;
}

// Send all queued recordings
void sendQueue() {
  if (queueCount == 0) {
    Serial.println("Queue empty");
    return;
  }
  
  int sent = 0;
  int failed = 0;
  
  // Get list of all queue files from SPIFFS
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  char queueFiles[5][48];  // Max 5 queue files
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
  
  Serial.printf("Found %d queue files\n", fileCount);
  
  // Try to send each queued recording
  for (int i = 0; i < fileCount; i++) {
    Serial.printf("Sending %s...\n", queueFiles[i]);
    
    // Show progress
    M5.Display.fillScreen(COLOR_BG_PRIMARY);
    char progressText[32];
    sprintf(progressText, "SEND %d/%d", i + 1, fileCount);
    drawTextSafe(progressText, 10, 50, 2, COLOR_WHITE);
    
    // Upload this file
    bool success = uploadRecording(queueFiles[i]);
    
    if (success) {
      SPIFFS.remove(queueFiles[i]);
      sent++;
      Serial.printf("Sent %s\n", queueFiles[i]);
    } else {
      failed++;
      Serial.printf("Failed %s\n", queueFiles[i]);
    }
    
    delay(500);  // Small delay between uploads
  }
  
  // Update queue count
  queueCount = failed;
  saveQueueCount(queueCount);
  
  // Show result
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  
  char resultText[32];
  if (sent > 0 && failed == 0) {
    // All succeeded - simple "SENT" message
    sprintf(resultText, "SENT %d", sent);
    drawTextSafe(resultText, 10, 50, 2, COLOR_GREEN);
  } else if (sent > 0 && failed > 0) {
    // Mixed results - show both (size 2 to prevent truncation)
    sprintf(resultText, "OK: %d", sent);
    drawTextSafe(resultText, 10, 40, 2, COLOR_GREEN);
    
    sprintf(resultText, "FAIL: %d", failed);
    drawTextSafe(resultText, 10, 65, 2, COLOR_RED);
  } else {
    // All failed
    sprintf(resultText, "FAIL %d", failed);
    drawTextSafe(resultText, 10, 50, 2, COLOR_RED);
  }
  
  delay(2000);
  Serial.printf("Queue complete: %d sent, %d failed\n", sent, failed);
}

void loop() {
  M5.update();
  
  // Manage screen power on ready screen (dim after 2s, off after 10s)
  if (!isRecording && !hasRecording && !screenIsOff) {
    uint32_t idleTime = millis() - lastInteractionTime;
    
    // Dim after 2 seconds
    if (idleTime >= SCREEN_DIM_DELAY && !screenIsDimmed) {
      M5.Display.setBrightness(SCREEN_BRIGHTNESS_DIMMED);
      screenIsDimmed = true;
      Serial.println("Ready screen: DIMMED");
    }
    
    // Turn off after 10 seconds
    if (idleTime >= IDLE_SCREEN_OFF_DELAY) {
      screenOff();
    }
  }
  
  // Button A: Record/Stop (also wakes screen if off)
  static unsigned long lastPressA = 0;
  if (M5.BtnA.wasPressed() && (millis() - lastPressA > 500)) {
    lastPressA = millis();
    lastInteractionTime = millis();
    
    if (screenIsOff) {
      screenOn();  // Just wake up, don't record yet
    } else if (!isRecording) {
      resetScreenPower();  // Reset brightness cycle
      startRecording();  // This now blocks until recording is done
    }
  }
  
  // Button B: Menu/Cancel (or wake screen)
  static unsigned long lastPressB = 0;
  if (M5.BtnB.wasPressed() && (millis() - lastPressB > 500)) {
    lastPressB = millis();
    lastInteractionTime = millis();
    
    if (screenIsOff) {
      screenOn();  // Just wake up
    } else if (hasRecording) {
      cancelRecording();  // Cancel current recording review
    } else {
      showMenu();  // Always show menu
      displayReady();  // Return to ready screen after menu
    }
  }
  
  // Power Button (C): Debug menu
  static unsigned long lastPressPWR = 0;
  if (M5.BtnPWR.wasPressed() && (millis() - lastPressPWR > 500)) {
    lastPressPWR = millis();
    lastInteractionTime = millis();
    
    if (screenIsOff) {
      screenOn();  // Wake up
    } else if (!isRecording && !hasRecording) {
      showDebugMenu();  // Show debug menu
      displayReady();  // Return to ready screen
    }
  }
  
  delay(100);
}

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

void writeWAVHeader(File file, uint32_t dataSize) {
  uint32_t fileSize = dataSize + 44 - 8;
  uint32_t byteRate = SAMPLE_RATE * 2;
  
  file.write((uint8_t*)"RIFF", 4);
  file.write((uint8_t*)&fileSize, 4);
  file.write((uint8_t*)"WAVE", 4);
  file.write((uint8_t*)"fmt ", 4);
  
  uint32_t fmtSize = 16;
  file.write((uint8_t*)&fmtSize, 4);
  uint16_t audioFormat = 1;
  file.write((uint8_t*)&audioFormat, 2);
  uint16_t numChannels = 1;
  file.write((uint8_t*)&numChannels, 2);
  uint32_t sampleRate = SAMPLE_RATE;
  file.write((uint8_t*)&sampleRate, 4);
  file.write((uint8_t*)&byteRate, 4);
  uint16_t blockAlign = 2;
  file.write((uint8_t*)&blockAlign, 2);
  uint16_t bitsPerSample = 16;
  file.write((uint8_t*)&bitsPerSample, 2);
  
  file.write((uint8_t*)"data", 4);
  file.write((uint8_t*)&dataSize, 4);
}

bool uploadRecording(const char* filename) {
  Serial.printf("Uploading %s...\n", filename);
  
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) {
    Serial.println("File open failed");
    return false;
  }
  
  size_t fileSize = file.size();
  Serial.printf("Size: %d bytes\n", fileSize);
  
  WiFiClientSecure client;
  client.setInsecure();
  
  if (!client.connect("www.koetori.com", 443)) {
    Serial.println("Connect failed");
    file.close();
    return false;
  }
  
  String boundary = "----B" + String(millis());
  
  String header = "--" + boundary + "\r\n";
  header += "Content-Disposition: form-data; name=\"audio\"; filename=\"rec.wav\"\r\n";
  header += "Content-Type: audio/wav\r\n\r\n";
  
  String footer = "\r\n--" + boundary + "\r\n";
  footer += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n" + String(DEVICE_ID) + "\r\n";
  footer += "--" + boundary + "\r\n";
  footer += "Content-Disposition: form-data; name=\"username\"\r\n\r\n" + String(USERNAME) + "\r\n";
  footer += "--" + boundary + "--\r\n";
  
  size_t totalSize = header.length() + fileSize + footer.length();
  
  client.print("POST /api/transcribe/device HTTP/1.1\r\n");
  client.print("Host: www.koetori.com\r\n");
  client.print("x-api-key: " + String(API_KEY) + "\r\n");
  client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  client.print("Content-Length: " + String(totalSize) + "\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(header);
  
  // Upload with progress bar!
  uint8_t buffer[512];
  size_t bytesSent = 0;
  resetScreenPower();  // Start brightness cycle
  while (file.available()) {
    size_t bytesRead = file.read(buffer, sizeof(buffer));
    client.write(buffer, bytesRead);
    bytesSent += bytesRead;
    
    // Update progress every 2KB
    if (bytesSent % 2048 < 512) {
      int percent = (bytesSent * 100) / fileSize;
      manageScreenPower();  // Handle dimming/off
      
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // "SENDING" - smaller text, left margin
      drawTextSafe("SENDING", 5, 30, 3, COLOR_WHITE);
      
      // Progress bar at bottom
      drawProgress(percent);
      
      Serial.printf("Send progress: %d%%\n", percent);
    }
  }
  
    file.close();
  client.print(footer);
  client.flush();
  
  unsigned long timeout = millis();
  while (!client.available() && millis() - timeout < 30000) {
    delay(10);
  }
  
  if (!client.available()) {
    Serial.println("Timeout");
    return false;
  }
  
  String response = "";
  while (client.available()) {
    response += (char)client.read();
  }
  client.stop();
  
  // Save response for parsing
  lastApiResponse = response;
  
  int httpCode = 0;
  int idx = response.indexOf("HTTP/1.1 ");
  if (idx >= 0) {
    httpCode = response.substring(idx + 9, idx + 12).toInt();
  }
  
  Serial.printf("HTTP: %d\n", httpCode);
  Serial.println("Response body:");
  Serial.println(response);
  
  if (httpCode == 200) {
    Serial.println("Upload success!");
    return true;
  } else {
    Serial.printf("Upload failed: HTTP %d\n", httpCode);
    return false;
  }
}

// Parse API response and extract stats (for future use)
UploadResponse parseUploadResponse(const String& response) {
  UploadResponse result;
  result.success = false;
  
  // Find JSON body (after headers)
  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart < 0) bodyStart = response.indexOf("\n\n");
  if (bodyStart < 0) return result;
  
  String json = response.substring(bodyStart + 4);
  json.trim();
  
  // Basic JSON parsing (simple approach for limited fields)
  result.success = json.indexOf("\"success\":true") >= 0 || json.indexOf("\"success\": true") >= 0;
  
  // Extract category
  int catStart = json.indexOf("\"category\":\"");
  if (catStart < 0) catStart = json.indexOf("\"category\": \"");
  if (catStart >= 0) {
    catStart = json.indexOf("\"", catStart + 12);
    int catEnd = json.indexOf("\"", catStart + 1);
    if (catEnd > catStart) {
      result.category = json.substring(catStart + 1, catEnd);
    }
  }
  
  // Extract confidence
  int confIdx = json.indexOf("\"confidence\":");
  if (confIdx < 0) confIdx = json.indexOf("\"confidence\": ");
  if (confIdx >= 0) {
    int numStart = json.indexOf(":", confIdx) + 1;
    int numEnd = json.indexOf(",", numStart);
    if (numEnd < 0) numEnd = json.indexOf("}", numStart);
    String confStr = json.substring(numStart, numEnd);
    confStr.trim();
    result.confidence = confStr.toFloat();
  }
  
  // Extract processing_time_ms
  int timeIdx = json.indexOf("\"processing_time_ms\":");
  if (timeIdx < 0) timeIdx = json.indexOf("\"processing_time_ms\": ");
  if (timeIdx >= 0) {
    int numStart = json.indexOf(":", timeIdx) + 1;
    int numEnd = json.indexOf(",", numStart);
    if (numEnd < 0) numEnd = json.indexOf("}", numStart);
    String timeStr = json.substring(numStart, numEnd);
    timeStr.trim();
    result.processingTimeMs = timeStr.toInt();
  }
  
  // Extract recording.duration_seconds (if present)
  int durIdx = json.indexOf("\"duration_seconds\":");
  if (durIdx < 0) durIdx = json.indexOf("\"duration_seconds\": ");
  if (durIdx >= 0) {
    int numStart = json.indexOf(":", durIdx) + 1;
    int numEnd = json.indexOf(",", numStart);
    if (numEnd < 0) numEnd = json.indexOf("}", numStart);
    String durStr = json.substring(numStart, numEnd);
    durStr.trim();
    result.durationSeconds = durStr.toFloat();
  }
  
  // Extract recording.file_size_kb (if present)
  int sizeIdx = json.indexOf("\"file_size_kb\":");
  if (sizeIdx < 0) sizeIdx = json.indexOf("\"file_size_kb\": ");
  if (sizeIdx >= 0) {
    int numStart = json.indexOf(":", sizeIdx) + 1;
    int numEnd = json.indexOf(",", numStart);
    if (numEnd < 0) numEnd = json.indexOf("}", numStart);
    String sizeStr = json.substring(numStart, numEnd);
    sizeStr.trim();
    result.fileSizeKb = sizeStr.toInt();
  }
  
  // Extract tokens.total (if present)
  int tokIdx = json.indexOf("\"total\":");
  if (tokIdx < 0) tokIdx = json.indexOf("\"total\": ");
  if (tokIdx >= 0) {
    int numStart = json.indexOf(":", tokIdx) + 1;
    int numEnd = json.indexOf(",", numStart);
    if (numEnd < 0) numEnd = json.indexOf("}", numStart);
    String tokStr = json.substring(numStart, numEnd);
    tokStr.trim();
    result.tokensUsed = tokStr.toInt();
  }
  
  // Extract quota.remaining (if present)
  int remIdx = json.indexOf("\"remaining\":");
  if (remIdx < 0) remIdx = json.indexOf("\"remaining\": ");
  if (remIdx >= 0) {
    int numStart = json.indexOf(":", remIdx) + 1;
    int numEnd = json.indexOf(",", numStart);
    if (numEnd < 0) numEnd = json.indexOf("}", numStart);
    String remStr = json.substring(numStart, numEnd);
    remStr.trim();
    result.tokensRemaining = remStr.toInt();
  }
  
  // Extract quota.percent_used (if present)
  int pctIdx = json.indexOf("\"percent_used\":");
  if (pctIdx < 0) pctIdx = json.indexOf("\"percent_used\": ");
  if (pctIdx >= 0) {
    int numStart = json.indexOf(":", pctIdx) + 1;
    int numEnd = json.indexOf(",", numStart);
    if (numEnd < 0) numEnd = json.indexOf("}", numStart);
    String pctStr = json.substring(numStart, numEnd);
    pctStr.trim();
    result.quotaPercent = pctStr.toInt();
  }
  
  return result;
}

// Bailey sprite: 135x240 pixels (full screen) - for boot screen

void showWiFiMenu() {
  Serial.println("Showing WiFi menu...");
  resetScreenPower();
  
  int selectedNetwork = 0;
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // Title
      drawTextSafe("WIFI", 10, 20, 3, COLOR_WHITE);
      
      // Show networks (compact layout)
      int y = 55;
      for (int i = 0; i < numNetworks; i++) {
        // Draw caret (size 1, at x=2)
        M5.Display.setTextSize(1);
        M5.Display.setCursor(2, y + 2);
        M5.Display.setTextColor((i == selectedNetwork) ? COLOR_GREEN : COLOR_BG_PRIMARY);
        M5.Display.print(">");
        
        // Draw SSID with safe truncation (starts at x=10, more space)
        uint16_t color = (i == selectedNetwork) ? COLOR_GREEN : COLOR_WHITE;
        drawTextSafe(wifiNetworks[i].ssid, 10, y, 1, color, 125);
        
        y += 15;  // Tighter spacing
      }
      
      // Back option
      y += 5;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 2);
      M5.Display.setTextColor((selectedNetwork == numNetworks) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      M5.Display.setCursor(10, y + 2);
      M5.Display.setTextColor((selectedNetwork == numNetworks) ? COLOR_GREEN : COLOR_WHITE);
      M5.Display.print("BACK");
      
      // Button hints
      drawButtonHint("SELECT", COLOR_GREEN, 210, "A");
      drawButtonHint("NEXT", COLOR_RED, 225, "B");
      
      needsRedraw = false;
    }
    
    // Button A: Select network or back
    if (M5.BtnA.wasPressed()) {
      Serial.printf("WiFi Menu: Selected option %d\n", selectedNetwork);
      delay(200);
      M5.update();
      
      if (selectedNetwork < numNetworks) {
        // Try to connect to selected network
        Serial.printf("Attempting to connect to: %s\n", wifiNetworks[selectedNetwork].ssid);
        
        // Show connecting screen
        M5.Display.fillScreen(COLOR_BG_PRIMARY);
        drawTextSafe("WIFI...", 10, 50, 3, COLOR_WHITE);
        // Show SSID on second line with size 1 to reduce truncation
        drawTextSafe(wifiNetworks[selectedNetwork].ssid, 10, 85, 1, COLOR_WHITE);
        // Show cancel hint
        drawButtonHint("CANCEL", COLOR_RED, 225, "B");
        
        // Attempt connection
        WiFi.disconnect();
        WiFi.begin(wifiNetworks[selectedNetwork].ssid, wifiNetworks[selectedNetwork].password);
        
        int attempts = 0;
        bool connected = false;
        bool cancelled = false;
        while (attempts < 20 && !connected && !cancelled) {  // 10 second timeout
          M5.update();
          
          // Check for cancel button (B)
          if (M5.BtnB.wasPressed()) {
            Serial.println("\nConnection cancelled by user");
            cancelled = true;
            break;
          }
          
          manageScreenPower();
          delay(500);
          Serial.print(".");
          attempts++;
          
          if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            break;
          }
        }
        Serial.println();
        
        // Show result
        M5.Display.fillScreen(COLOR_BG_PRIMARY);
        
        if (cancelled) {
          drawTextSafe("CANCEL", 10, 50, 3, COLOR_YELLOW);
          WiFi.disconnect();
          Serial.println("Connection cancelled");
        } else if (connected) {
          drawTextSafe("SUCCESS", 10, 50, 2, COLOR_GREEN);
          lastSuccessfulNetwork = selectedNetwork;
          saveLastNetwork(selectedNetwork);
          WiFi.setSleep(true);
          Serial.println("WiFi connected!");
          Serial.print("IP: ");
          Serial.println(WiFi.localIP());
        } else {
          drawTextSafe("FAILED", 10, 50, 2, COLOR_RED);
          WiFi.disconnect();
          Serial.println("Connection failed");
        }
        
        // Show OK button hint
        drawButtonHint("OK", COLOR_GREEN, 210, "A");
        
        // Wait for button press to continue
        while (true) {
          M5.update();
          if (M5.BtnA.wasPressed()) {
            delay(200);
            return;  // Return to main menu
          }
          delay(50);
        }
      } else {
        // Back selected
        return;
      }
    }
    
    // Button B: Next network
    if (M5.BtnB.wasPressed()) {
      selectedNetwork = (selectedNetwork + 1) % (numNetworks + 1);  // +1 for BACK option
      Serial.printf("WiFi Menu: Changed to option %d\n", selectedNetwork);
      needsRedraw = true;
      delay(200);
      M5.update();
    }
    
    delay(50);
  }
}

void showMenu() {
  Serial.println("Showing menu...");
  resetScreenPower();
  
  int selectedOption = 0;
  // Skip SEND QUEUE if empty, start at WIFI
  if (queueCount == 0) {
    selectedOption = 1;
  }
  int numOptions = 4;  // SEND QUEUE, CONNECT WIFI, REC MODE, BACK
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    // Only redraw when needed (not every loop iteration)
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // Title
      drawTextSafe("MENU", 10, 20, 3, COLOR_WHITE);
      
      // Storage indicator (top right, small)
      size_t totalBytes = SPIFFS.totalBytes();
      size_t usedBytes = SPIFFS.usedBytes();
      int usedPercent = (usedBytes * 100) / totalBytes;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(70, 22);
      if (usedPercent > 80) {
        M5.Display.setTextColor(COLOR_RED);
      } else if (usedPercent > 60) {
        M5.Display.setTextColor(COLOR_YELLOW);
      } else {
        M5.Display.setTextColor(COLOR_GRAY);
      }
      M5.Display.printf("%d%%", usedPercent);
      
      // Option 1: Send Queue
      int y = 65;
      // Caret (size 1)
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 3);
      M5.Display.setTextColor((selectedOption == 0 && queueCount > 0) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      // Text (size 1, more compact)
      M5.Display.setTextSize(1);
      M5.Display.setCursor(10, y + 3);
      if (queueCount > 0) {
        M5.Display.setTextColor((selectedOption == 0) ? COLOR_GREEN : COLOR_WHITE);
        M5.Display.printf("SEND %d", queueCount);
      } else {
        M5.Display.setTextColor(COLOR_GRAY);
        M5.Display.print("SEND -");
      }
      
      // Option 2: Connect WiFi
      y += 18;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 3);
      M5.Display.setTextColor((selectedOption == 1) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      M5.Display.setCursor(10, y + 3);
      M5.Display.setTextColor((selectedOption == 1) ? COLOR_GREEN : COLOR_WHITE);
      M5.Display.print("WIFI");
      
      // Option 3: Recording Mode Toggle
      y += 18;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 3);
      M5.Display.setTextColor((selectedOption == 2) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      M5.Display.setCursor(10, y + 3);
      M5.Display.setTextColor((selectedOption == 2) ? COLOR_GREEN : COLOR_WHITE);
      M5.Display.printf("REC: %ds", isLongRecordingMode ? RECORDING_TIME_LONG : RECORDING_TIME_NORMAL);
      
      // Option 4: Back
      y += 18;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 3);
      M5.Display.setTextColor((selectedOption == 3) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      M5.Display.setCursor(10, y + 3);
      M5.Display.setTextColor((selectedOption == 3) ? COLOR_GREEN : COLOR_WHITE);
      M5.Display.print("BACK");
      
      // Button hints
      drawButtonHint("SELECT", COLOR_GREEN, 210, "A");
      drawButtonHint("NEXT", COLOR_RED, 225, "B");
      
      needsRedraw = false;
    }
    
    // Button A: Select option
    if (M5.BtnA.wasPressed()) {
      Serial.printf("Menu: Selected option %d\n", selectedOption);
      delay(200);
      M5.update();
      
      if (selectedOption == 0) {
        // Send queue - only if we have items
        if (queueCount > 0) {
          sendQueue();
          return;
        }
        // If queue empty, do nothing (shouldn't be selected anyway)
      } else if (selectedOption == 1) {
        // Connect WiFi - show WiFi network selection menu
        showWiFiMenu();
        needsRedraw = true;  // Redraw menu after returning from WiFi menu
      } else if (selectedOption == 2) {
        // Toggle recording mode
        isLongRecordingMode = !isLongRecordingMode;
        Serial.printf("Recording mode: %ds\n", isLongRecordingMode ? RECORDING_TIME_LONG : RECORDING_TIME_NORMAL);
        needsRedraw = true;
      } else if (selectedOption == 3) {
        // Back - return to ready screen
        displayReady();
        return;
      }
    }
    
    // Button B: Next option
    if (M5.BtnB.wasPressed()) {
      // Skip over SEND option if queue is empty
      do {
        selectedOption = (selectedOption + 1) % numOptions;
      } while (selectedOption == 0 && queueCount == 0);
      
      Serial.printf("Menu: Changed to option %d\n", selectedOption);
      needsRedraw = true;
      delay(200);
      M5.update();
    }
    
    delay(50);
  }
}

// Debug Menu
void showDebugMenu() {
  Serial.println("\n=== DEBUG MENU ===");
  resetScreenPower();
  
  int selectedOption = 0;
  int numOptions = 5;  // Text Test, LED Test, Sprites, Device Info, Back
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // Title
      drawTextSafe("DEBUG", 10, 15, 2, COLOR_YELLOW);
      
      // Options
      const char* options[] = {"Text Test", "LED Test", "Sprites", "Device", "Back"};
      int y = 50;
      for (int i = 0; i < numOptions; i++) {
        // Draw caret
        M5.Display.setTextSize(1);
        M5.Display.setCursor(2, y + 2);
        M5.Display.setTextColor((i == selectedOption) ? COLOR_GREEN : COLOR_BG_PRIMARY);
        M5.Display.print(">");
        // Draw text
        drawTextSafe(options[i], 10, y, 1, (i == selectedOption) ? COLOR_GREEN : COLOR_WHITE);
        y += 15;  // Tighter spacing
      }
      
      // Button hints
      drawButtonHint("SELECT", COLOR_GREEN, 210, "A");
      drawButtonHint("NEXT", COLOR_RED, 225, "B");
      
      needsRedraw = false;
    }
    
    // Button A: Select
    if (M5.BtnA.wasPressed()) {
      delay(200);
      if (selectedOption == 0) {
        showTextTest();
      } else if (selectedOption == 1) {
        showLEDTest();
      } else if (selectedOption == 2) {
        showSpriteViewer();
      } else if (selectedOption == 3) {
        showDeviceInfo();
      } else if (selectedOption == 4) {
        return;  // Back
      }
      needsRedraw = true;
    }
    
    // Button B: Next
    if (M5.BtnB.wasPressed()) {
      selectedOption = (selectedOption + 1) % numOptions;
      needsRedraw = true;
      delay(200);
    }
    
    delay(50);
  }
}

// Text Size Test
void showTextTest() {
  Serial.println("\n=== TEXT SIZE TEST ===");
  resetScreenPower();
  
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  drawTextSafe("TEXT TEST", 5, 30, 2, COLOR_YELLOW);
  drawTextSafe("Check Serial", 5, 60, 1, COLOR_WHITE);
  drawButtonHint("OK", COLOR_GREEN, 210, "A");
  
  // Print text size info to Serial
  Serial.println("\n--- Common Text Elements ---");
  previewTextSize("RECORDING", 4);
  previewTextSize("REC", 4);
  previewTextSize("SENDING", 3);
  previewTextSize("SUCCESS", 3);
  previewTextSize("MENU", 3);
  
  Serial.println("\n--- Testing Safe Draw ---");
  Serial.println("Screen width: 135px");
  
  const char* testTexts[] = {"SHORT", "MEDIUM TEXT", "THIS IS VERY LONG"};
  for (int i = 0; i < 3; i++) {
    for (int size = 1; size <= 4; size++) {
      int width = getTextWidth(testTexts[i], size);
      bool fits = (width + 10 <= 135);
      Serial.printf("'%s' size %d: %dpx %s\n", testTexts[i], size, width, fits ? "✓" : "✗");
    }
  }
  
  Serial.println("\nPress A to continue...\n");
  
  while (true) {
    M5.update();
    if (M5.BtnA.wasPressed()) {
      delay(200);
      return;
    }
    delay(50);
  }
}

// LED Test
void showLEDTest() {
  Serial.println("\n=== LED TEST ===");
  resetScreenPower();
  
  int state = 0;  // 0=off, 1=on
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      drawTextSafe("LED TEST", 5, 20, 2, COLOR_YELLOW);
      
      M5.Display.setTextSize(1);
      M5.Display.setCursor(5, 60);
      M5.Display.setTextColor(COLOR_WHITE);
      M5.Display.printf("State: %s", state == 0 ? "OFF" : "ON");
      
      drawButtonHint("TOGGLE", COLOR_GREEN, 210, "A");
      drawButtonHint("BACK", COLOR_RED, 225, "B");
      
      // Control LED
      // Note: M5StickC Plus2 has a red LED on GPIO 19
      pinMode(19, OUTPUT);
      digitalWrite(19, state == 1 ? HIGH : LOW);
      
      Serial.printf("LED: %s\n", state == 0 ? "OFF" : "ON");
      
      needsRedraw = false;
    }
    
    if (M5.BtnA.wasPressed()) {
      state = 1 - state;  // Toggle
      needsRedraw = true;
      delay(200);
    }
    
    if (M5.BtnB.wasPressed()) {
      digitalWrite(19, LOW);  // Turn off on exit
      delay(200);
      return;
    }
    
    delay(50);
  }
}

// Sprite Viewer - scroll through all sprites
void showSpriteViewer() {
  Serial.println("\n=== SPRITE VIEWER ===");
  resetScreenPower();
  
  int currentSprite = 0;
  int numSprites = 2;  // Bailey, Gengar
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // Draw current sprite
      if (currentSprite == 0) {
        drawBailey();
        // Label
        M5.Display.setTextSize(1);
        M5.Display.setCursor(5, 5);
        M5.Display.setTextColor(COLOR_YELLOW, COLOR_BG_PRIMARY);
        M5.Display.print("Bailey");
      } else if (currentSprite == 1) {
        drawGengar(4, 35);
        // Label
        M5.Display.setTextSize(1);
        M5.Display.setCursor(5, 5);
        M5.Display.setTextColor(COLOR_YELLOW, COLOR_BG_PRIMARY);
        M5.Display.print("Gengar");
      }
      
      // Button hints at bottom
      M5.Display.setTextSize(1);
      M5.Display.setCursor(5, 225);
      M5.Display.setTextColor(COLOR_WHITE, COLOR_BG_PRIMARY);
      M5.Display.print("A:Next B:Exit");
      
      needsRedraw = false;
    }
    
    // Button A: Next sprite
    if (M5.BtnA.wasPressed()) {
      currentSprite = (currentSprite + 1) % numSprites;
      needsRedraw = true;
      delay(200);
    }
    
    // Button B: Exit
    if (M5.BtnB.wasPressed()) {
      delay(200);
      return;
    }
    
    delay(50);
  }
}

// Device Info
void showDeviceInfo() {
  Serial.println("\n=== DEVICE INFO ===");
  resetScreenPower();
  
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  drawTextSafe("INFO", 5, 10, 2, COLOR_YELLOW);
  
  // Device ID
  M5.Display.setTextSize(1);
  M5.Display.setCursor(2, 35);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.printf("ID: %s", DEVICE_ID);
  
  // CPU Freq
  M5.Display.setCursor(2, 50);
  M5.Display.printf("CPU: %dMHz", getCpuFrequencyMhz());
  
  // Storage
  size_t totalKB = SPIFFS.totalBytes() / 1024;
  size_t usedKB = SPIFFS.usedBytes() / 1024;
  M5.Display.setCursor(2, 65);
  M5.Display.printf("Storage: %dK/%dK", usedKB, totalKB);
  
  // WiFi MAC
  M5.Display.setCursor(2, 80);
  M5.Display.printf("MAC: %s", WiFi.macAddress().c_str());
  
  // Battery
  M5.Display.setCursor(2, 95);
  M5.Display.printf("Battery: %d%%", M5.Power.getBatteryLevel());
  
  // Queue
  M5.Display.setCursor(2, 110);
  M5.Display.printf("Queue: %d items", queueCount);
  
  drawButtonHint("OK", COLOR_GREEN, 210, "A");
  
  // Print to serial too
  Serial.println("\n--- Device Info ---");
  Serial.printf("Device ID: %s\n", DEVICE_ID);
  Serial.printf("Username: %s\n", USERNAME);
  Serial.printf("CPU: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Storage: %d KB / %d KB\n", usedKB, totalKB);
  Serial.printf("WiFi MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Battery: %d%%\n", M5.Power.getBatteryLevel());
  Serial.printf("Queue: %d items\n", queueCount);
  Serial.println();
  
  while (true) {
    M5.update();
    if (M5.BtnA.wasPressed()) {
      delay(200);
      return;
    }
    delay(50);
  }
}

