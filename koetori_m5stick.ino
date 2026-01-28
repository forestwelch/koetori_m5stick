/*
 * Koetori M5StickC PLUS2 - Voice Recording Device (BLE)
 *
 * Records audio using M5.Mic and streams to iOS via BLE. The iPhone app
 * uploads to Koetori API for transcription and categorization.
 *
 * See README.md and TESTING.md for setup and testing.
 */

#include <M5StickCPlus2.h>
#include <SPIFFS.h>
#include <esp_log.h>
#include "secrets.h"
#include "config.h"
#include "types.h"
#include "globals.h"
#include "sprites.h"
#include "display.h"
#include "menus.h"
#include "utils.h"
#include "recording.h"
#include "ble_handler.h"

// Global variable definitions
bool isRecording = false;
bool hasRecording = false;
bool screenIsOff = false;
bool screenIsDimmed = false;
uint32_t recordingStartTime = 0;
uint32_t lastInteractionTime = 0;
uint32_t screenStateStartTime = 0;
int16_t* recordingBuffer = nullptr;
uint32_t samplesRecorded = 0;
int queueCount = 0;
bool isLongRecordingMode = false;
String lastApiResponse = "";

void setup() {
  esp_log_level_set("*", ESP_LOG_NONE);

  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== Koetori M5StickC PLUS2 (BLE) ===");
  Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());

  auto cfg = M5.config();
  cfg.internal_imu = false;
  M5.begin(cfg);

  M5.Display.setRotation(0);
  M5.Display.setBrightness(SCREEN_BRIGHTNESS);
  M5.Display.fillScreen(COLOR_BG_PRIMARY);

  drawBailey();

  Serial.println("M5 initialized");

  if (!SPIFFS.begin(false)) {
    SPIFFS.begin(true);
  }
  Serial.println("SPIFFS OK");

  queueCount = loadQueueCount();
  Serial.printf("Queue: %d recordings\n", queueCount);

  recordingBuffer = (int16_t*)malloc(MAX_RECORDING_SIZE);
  if (!recordingBuffer) {
    Serial.println("Failed to allocate buffer!");
    while (1) delay(1000);
  }
  Serial.println("Buffer allocated");

  auto mic_cfg = M5.Mic.config();
  mic_cfg.noise_filter_level = 8;
  M5.Mic.config(mic_cfg);

  if (M5.Mic.begin()) {
    Serial.println("Mic OK (with noise filter)");
  } else {
    Serial.println("Mic failed!");
  }

  initBLE();

  lastInteractionTime = millis();
  displayReady();
  Serial.println("Setup complete\n");
}
