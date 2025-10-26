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
#include "menus.h"    // Menu functions
#include "utils.h"    // Utility functions
#include "recording.h"  // Recording functions

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

// Queue Management Functions


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


