/*
 * BLE handler implementation - Koetori M5StickC PLUS2
 * Streams audio to iOS via BLE; protocol matches koetori-ios expectations.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <SPIFFS.h>
#include "config.h"
#include "secrets.h"
#include "ble_handler.h"
#include "globals.h"

static BLEServer* pServer = nullptr;
static BLECharacteristic* pAudioChar = nullptr;
static BLECharacteristic* pControlChar = nullptr;
static BLECharacteristic* pStatusChar = nullptr;
static bool deviceConnected = false;
static bool advertising = false;

// Called when a client connects
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* p) override {
    deviceConnected = true;
    Serial.println("BLE client connected");
  }
  void onDisconnect(BLEServer* p) override {
    deviceConnected = false;
    advertising = false;
    Serial.println("BLE client disconnected");
    p->getAdvertising()->start();
    advertising = true;
  }
};

// Called when iPhone writes to Control (e.g. ACK, SUCCESS:cat:conf)
class ControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String val = pChar->getValue();
    if (val.length() == 0) return;
    Serial.printf("BLE Control RX: %s\n", val.c_str());
    if (val.startsWith("SUCCESS:")) {
      lastApiResponse = val;  // Store for display (category, confidence)
    }
  }
};

void initBLE() {
  String name = String(BLE_DEVICE_NAME_PREFIX) + "-" + String(DEVICE_ID);
  BLEDevice::init(name.c_str());

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  // Audio stream (notify) - 512 bytes per notification
  pAudioChar = pService->createCharacteristic(
    BLE_CHAR_AUDIO_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pAudioChar->addDescriptor(new BLE2902());

  // Control (read, write, notify) - START/END/ERROR from M5; ACK/SUCCESS from iPhone
  pControlChar = pService->createCharacteristic(
    BLE_CHAR_CONTROL_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
  );
  pControlChar->setCallbacks(new ControlCallbacks());
  pControlChar->addDescriptor(new BLE2902());

  // Status (read, notify) - battery, recording state, queue
  pStatusChar = pService->createCharacteristic(
    BLE_CHAR_STATUS_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);

  pAdvertising->start();
  advertising = true;
  Serial.println("BLE initialized, advertising");
}

void startBLEAdvertising() {
  if (deviceConnected) return;
  if (!advertising) {
    BLEDevice::getAdvertising()->start();
    advertising = true;
    Serial.println("BLE advertising started");
  }
}

void stopBLEAdvertising() {
  BLEDevice::getAdvertising()->stop();
  advertising = false;
}

bool isBLEConnected() {
  return deviceConnected;
}

static void sendControl(const char* msg) {
  if (!pControlChar || !deviceConnected) return;
  pControlChar->setValue((uint8_t*)msg, strlen(msg));
  pControlChar->notify();
}

bool streamViaBLE(const char* filepath) {
  if (!deviceConnected || !pAudioChar || !pControlChar) {
    Serial.println("BLE not connected");
    return false;
  }

  File f = SPIFFS.open(filepath, FILE_READ);
  if (!f) {
    Serial.printf("streamViaBLE: cannot open %s\n", filepath);
    sendControl("ERROR:file_open");
    return false;
  }

  size_t totalBytes = f.size();
  if (totalBytes <= 44) {
    f.close();
    sendControl("ERROR:file_too_small");
    return false;
  }

  // Skip WAV header (44 bytes)
  f.seek(44, SeekSet);
  size_t dataBytes = totalBytes - 44;
  uint32_t totalChunks = (dataBytes + BLE_AUDIO_PAYLOAD_SIZE - 1) / BLE_AUDIO_PAYLOAD_SIZE;

  uint32_t ts = millis();
  char startMsg[80];
  snprintf(startMsg, sizeof(startMsg), "START:%u:%u:1", (unsigned)ts, (unsigned)SAMPLE_RATE);
  sendControl(startMsg);

  uint8_t chunkBuf[BLE_AUDIO_CHUNK_SIZE];
  uint32_t chunkIndex = 0;

  while (chunkIndex < totalChunks && deviceConnected) {
    memset(chunkBuf, 0, BLE_AUDIO_CHUNK_SIZE);
    chunkBuf[0] = (uint8_t)(chunkIndex & 0xff);
    chunkBuf[1] = (uint8_t)((chunkIndex >> 8) & 0xff);

    size_t toRead = BLE_AUDIO_PAYLOAD_SIZE;
    if (chunkIndex == totalChunks - 1) {
      size_t remainder = dataBytes - (chunkIndex * BLE_AUDIO_PAYLOAD_SIZE);
      if (remainder < BLE_AUDIO_PAYLOAD_SIZE) toRead = remainder;
    }
    size_t n = f.read(chunkBuf + 2, toRead);
    if (n == 0) break;

    pAudioChar->setValue(chunkBuf, 2 + n);
    pAudioChar->notify();
    chunkIndex++;
    delay(10);  // 10ms between chunks so iOS doesn't drop notifications (~100 chunks/sec max)
  }

  f.close();

  if (!deviceConnected) {
    Serial.println("BLE disconnected during stream");
    return false;
  }

  char endMsg[32];
  snprintf(endMsg, sizeof(endMsg), "END:%u", (unsigned)chunkIndex);
  sendControl(endMsg);

  Serial.printf("BLE streamed %s: %u chunks\n", filepath, (unsigned)chunkIndex);
  return true;
}

void bleNotifyStatus(int batteryPercent, bool recording, int queueCount) {
  if (!pStatusChar || !deviceConnected) return;
  char buf[32];
  snprintf(buf, sizeof(buf), "B:%d R:%d Q:%d", batteryPercent, recording ? 1 : 0, queueCount);
  pStatusChar->setValue((uint8_t*)buf, strlen(buf));
  pStatusChar->notify();
}
