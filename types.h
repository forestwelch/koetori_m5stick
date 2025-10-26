/*
 * Type definitions for Koetori M5StickC PLUS2
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>  // For String

// Struct to hold API response data
struct UploadResponse {
  bool success;
  String category;
  float confidence;
  int processingTimeMs;
  float durationSeconds;
  int fileSizeKb;
  int tokensUsed;
  int tokensRemaining;
  int quotaPercent;
};

#endif // TYPES_H

