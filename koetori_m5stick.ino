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
