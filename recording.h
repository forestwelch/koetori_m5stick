/*
 * Recording functions for Koetori M5StickC PLUS2
 */

#ifndef RECORDING_H
#define RECORDING_H

#include <Arduino.h>

// Recording control
void startRecording();
void stopRecording();
void cancelRecording();

// Review and upload
void displayReview();
void saveAndUpload();

// Queue and BLE send
void sendQueue();

#endif // RECORDING_H

