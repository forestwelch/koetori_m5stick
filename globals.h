/*
 * Global variable declarations for Koetori M5StickC PLUS2
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

// State variables
extern bool isRecording;
extern bool hasRecording;
extern bool screenIsOff;
extern bool screenIsDimmed;
extern uint32_t recordingStartTime;
extern uint32_t lastInteractionTime;
extern uint32_t screenStateStartTime;

// Recording buffers
extern int16_t* recordingBuffer;
extern uint32_t samplesRecorded;

// Queue
extern int queueCount;

// BLE: connection handled in ble_handler; lastApiResponse set when iPhone sends SUCCESS

// Recording mode
extern bool isLongRecordingMode;

// API response storage
extern String lastApiResponse;

#endif // GLOBALS_H

