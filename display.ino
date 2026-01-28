#include "ble_handler.h"

int getTextWidth(const char* text, int textSize) {
  M5.Display.setTextSize(textSize);
  return M5.Display.textWidth(text);
}

// Safe text drawing with overflow check and truncation
// Returns true if text fit, false if truncated
bool drawTextSafe(const char* text, int x, int y, int textSize, uint16_t color, int maxWidth) {
  M5.Display.setTextSize(textSize);
  M5.Display.setTextColor(color);
  
  // Check if text fits
  int textWidth = M5.Display.textWidth(text);
  
  if (textWidth + x <= maxWidth) {
    // Text fits perfectly
    M5.Display.setCursor(x, y);
    M5.Display.print(text);
    return true;
  } else {
    // Text too long - truncate with "..."
    String truncated = String(text);
    while (truncated.length() > 0) {
      truncated = truncated.substring(0, truncated.length() - 1);
      String withEllipsis = truncated + "..";
      int newWidth = M5.Display.textWidth(withEllipsis.c_str());
      
      if (newWidth + x <= maxWidth) {
        M5.Display.setCursor(x, y);
        M5.Display.print(withEllipsis);
        Serial.printf("WARN: Text truncated: '%s' -> '%s' (size %d, width %d->%d)\n", 
                      text, withEllipsis.c_str(), textSize, textWidth, newWidth);
        return false;
      }
    }
    
    // Fallback: just print "..."
    M5.Display.setCursor(x, y);
    M5.Display.print("..");
    Serial.printf("WARN: Text severely truncated: '%s' (size %d, width %d)\n", 
                  text, textSize, textWidth);
    return false;
  }
}

// Calculate optimal text size to fit text within maxWidth
// Returns recommended text size (1-4)
int calculateOptimalTextSize(const char* text, int maxWidth) {
  for (int size = 4; size >= 1; size--) {
    M5.Display.setTextSize(size);
    int width = M5.Display.textWidth(text);
    if (width <= maxWidth - 10) {  // 10px margin
      Serial.printf("Optimal size for '%s': %d (width: %d/%d)\n", text, size, width, maxWidth);
      return size;
    }
  }
  Serial.printf("WARN: Even size 1 too large for '%s'\n", text);
  return 1;
}

// Preview text dimensions without drawing (for debugging)
void previewTextSize(const char* text, int textSize) {
  M5.Display.setTextSize(textSize);
  int width = M5.Display.textWidth(text);
  int height = 8 * textSize;  // Approximate height
  Serial.printf("TEXT PREVIEW: '%s' size=%d -> %dx%d px\n", text, textSize, width, height);
}

void drawButtonHint(const char* label, uint16_t color, int y, const char* button) {
  int x = 10;  // Left margin
  
  // Draw filled circle
  M5.Display.fillCircle(x + 4, y, 5, color);
  
  // Draw letter inside circle (smaller, black text)
  M5.Display.setTextSize(1);
  M5.Display.setCursor(x + 2, y - 3);
  M5.Display.setTextColor(COLOR_BG_PRIMARY);  // Black text on colored circle
  M5.Display.print(button);
  
  // Draw label next to circle
  M5.Display.setTextSize(1);  // Smaller for portrait
  M5.Display.setCursor(x + 14, y - 3);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.println(label);
}

// Helper: Draw progress bar (portrait mode)
void drawProgress(int percent) {
  int barWidth = 115;  // Fits portrait width (135px) with margins
  int barHeight = 6;   // Slightly taller for visibility
  int y = 200;  // Near bottom of screen
  
  // Background bar (very dim)
  M5.Display.drawRect(10, y, barWidth, barHeight, 0x2104);
  
  // Fill bar (subtle color based on progress)
  int fillWidth = (barWidth * percent) / 100;
  if (fillWidth > 0) {
    M5.Display.fillRect(10, y, fillWidth, barHeight, 0x4208);
  }
  
  // Show percentage (tiny text)
  M5.Display.setTextSize(1);
  M5.Display.setCursor(95, y + 8);
  M5.Display.setTextColor(0x4208);
  M5.Display.printf("%d%%", percent);
}

// Minimal idle screen for battery: BLE + battery + one hint. No sprite, no menu hints.
void displayReady() {
  resetScreenPower();
  lastInteractionTime = millis();

  M5.Display.fillScreen(COLOR_BG_PRIMARY);

  if (isBLEConnected()) {
    drawTextSafe("BLE ON", 2, 5, 1, COLOR_GREEN, 90);
  } else {
    drawTextSafe("SEARCH", 2, 5, 1, COLOR_YELLOW, 90);
  }

  int batteryLevel = M5.Power.getBatteryLevel();
  M5.Display.setTextSize(1);
  M5.Display.setCursor(95, 5);
  if (batteryLevel > 50) {
    M5.Display.setTextColor(COLOR_GREEN);
  } else if (batteryLevel > 20) {
    M5.Display.setTextColor(COLOR_YELLOW);
  } else {
    M5.Display.setTextColor(COLOR_RED);
  }
  M5.Display.printf("%d%%", batteryLevel);

  // Single purpose: A = record/stop. One hint only.
  drawButtonHint("A = REC", COLOR_GREEN, 220, "A");

  Serial.println("Ready");
}

void displayError(const char* msg) {
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  
  // "ERROR" in red - left aligned, consistent
  drawTextSafe("ERROR", 10, 50, 4, COLOR_RED);
  
  // Button hint: A = retry (at bottom)
  drawButtonHint("RETRY", COLOR_GREEN, 210, "A");
  
  Serial.printf("Error: %s\n", msg);
}
