/*
 * BLE handler for Koetori M5StickC PLUS2
 * Streams audio to iOS app over BLE; iPhone handles API upload.
 */

#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <Arduino.h>

// Lifecycle
void initBLE();
void startBLEAdvertising();
void stopBLEAdvertising();

// Connection
bool isBLEConnected();

// Streaming: send WAV file in chunks (512-byte packets: 2-byte index + 510-byte payload)
// Sends START, then chunks, then END on Control characteristic; audio bytes on Audio characteristic.
bool streamViaBLE(const char* filepath);

// Stream directly from RAM (no SPIFFS). Applies 10x gain, same chunk format as file path.
bool streamViaBLEFromSamples(int16_t* samples, uint32_t sampleCount);

// Status updates (call from main loop or after recording)
void bleNotifyStatus(int batteryPercent, bool recording, int queueCount);

#endif // BLE_HANDLER_H
