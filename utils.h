/*
 * Utility functions for Koetori M5StickC PLUS2
 */

#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <FS.h>

// Screen power management
void manageScreenPowerNoButton();
void manageScreenPower();
void resetScreenPower();
void screenOff();
void screenOn();

// Storage/SPIFFS helpers
int loadQueueCount();
void saveQueueCount(int count);
int loadLastNetwork();
void saveLastNetwork(int networkIndex);
bool addToQueue();

// WAV file utilities
void writeWAVHeader(File file, uint32_t dataSize);

#endif // UTILS_H

