/*
 * Display and text utility functions for Koetori M5StickC PLUS2
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Text utilities
int getTextWidth(const char* text, int textSize);
bool drawTextSafe(const char* text, int x, int y, int textSize, uint16_t color, int maxWidth = 135);
int calculateOptimalTextSize(const char* text, int maxWidth = 135);
void previewTextSize(const char* text, int textSize);

// UI helpers
void drawButtonHint(const char* label, uint16_t color, int y, const char* button);
void drawProgress(int percent);

// Display screens
void displayReady();
void displayError(const char* msg);

#endif // DISPLAY_H

