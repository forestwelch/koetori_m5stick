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

// Queue and network upload
void sendQueue();
bool uploadRecording(const char* filename = "/rec.wav");
UploadResponse parseUploadResponse(const String& response);

#endif // RECORDING_H

