void showWiFiMenu() {
  Serial.println("Showing WiFi menu...");
  resetScreenPower();
  
  int selectedNetwork = 0;
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // Title
      drawTextSafe("WIFI", 10, 20, 3, COLOR_WHITE);
      
      // Show networks (compact layout)
      int y = 55;
      for (int i = 0; i < numNetworks; i++) {
        // Draw caret (size 1, at x=2)
        M5.Display.setTextSize(1);
        M5.Display.setCursor(2, y + 2);
        M5.Display.setTextColor((i == selectedNetwork) ? COLOR_GREEN : COLOR_BG_PRIMARY);
        M5.Display.print(">");
        
        // Draw SSID with safe truncation (starts at x=10, more space)
        uint16_t color = (i == selectedNetwork) ? COLOR_GREEN : COLOR_WHITE;
        drawTextSafe(wifiNetworks[i].ssid, 10, y, 1, color, 125);
        
        y += 15;  // Tighter spacing
      }
      
      // Back option
      y += 5;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 2);
      M5.Display.setTextColor((selectedNetwork == numNetworks) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      M5.Display.setCursor(10, y + 2);
      M5.Display.setTextColor((selectedNetwork == numNetworks) ? COLOR_GREEN : COLOR_WHITE);
      M5.Display.print("BACK");
      
      // Button hints
      drawButtonHint("SELECT", COLOR_GREEN, 210, "A");
      drawButtonHint("NEXT", COLOR_RED, 225, "B");
      
      needsRedraw = false;
    }
    
    // Button A: Select network or back
    if (M5.BtnA.wasPressed()) {
      Serial.printf("WiFi Menu: Selected option %d\n", selectedNetwork);
      delay(200);
      M5.update();
      
      if (selectedNetwork < numNetworks) {
        // Try to connect to selected network
        Serial.printf("Attempting to connect to: %s\n", wifiNetworks[selectedNetwork].ssid);
        
        // Show connecting screen
        M5.Display.fillScreen(COLOR_BG_PRIMARY);
        drawTextSafe("WIFI...", 10, 50, 3, COLOR_WHITE);
        // Show SSID on second line with size 1 to reduce truncation
        drawTextSafe(wifiNetworks[selectedNetwork].ssid, 10, 85, 1, COLOR_WHITE);
        // Show cancel hint
        drawButtonHint("CANCEL", COLOR_RED, 225, "B");
        
        // Attempt connection
        WiFi.disconnect();
        WiFi.begin(wifiNetworks[selectedNetwork].ssid, wifiNetworks[selectedNetwork].password);
        
        int attempts = 0;
        bool connected = false;
        bool cancelled = false;
        while (attempts < 20 && !connected && !cancelled) {  // 10 second timeout
          M5.update();
          
          // Check for cancel button (B)
          if (M5.BtnB.wasPressed()) {
            Serial.println("\nConnection cancelled by user");
            cancelled = true;
            break;
          }
          
          manageScreenPower();
          delay(500);
          Serial.print(".");
          attempts++;
          
          if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            break;
          }
        }
        Serial.println();
        
        // Show result
        M5.Display.fillScreen(COLOR_BG_PRIMARY);
        
        if (cancelled) {
          drawTextSafe("CANCEL", 10, 50, 3, COLOR_YELLOW);
          WiFi.disconnect();
          Serial.println("Connection cancelled");
        } else if (connected) {
          drawTextSafe("SUCCESS", 10, 50, 2, COLOR_GREEN);
          lastSuccessfulNetwork = selectedNetwork;
          saveLastNetwork(selectedNetwork);
          WiFi.setSleep(true);
          Serial.println("WiFi connected!");
          Serial.print("IP: ");
          Serial.println(WiFi.localIP());
        } else {
          drawTextSafe("FAILED", 10, 50, 2, COLOR_RED);
          WiFi.disconnect();
          Serial.println("Connection failed");
        }
        
        // Show OK button hint
        drawButtonHint("OK", COLOR_GREEN, 210, "A");
        
        // Wait for button press to continue
        while (true) {
          M5.update();
          if (M5.BtnA.wasPressed()) {
            delay(200);
            return;  // Return to main menu
          }
          delay(50);
        }
      } else {
        // Back selected
        return;
      }
    }
    
    // Button B: Next network
    if (M5.BtnB.wasPressed()) {
      selectedNetwork = (selectedNetwork + 1) % (numNetworks + 1);  // +1 for BACK option
      Serial.printf("WiFi Menu: Changed to option %d\n", selectedNetwork);
      needsRedraw = true;
      delay(200);
      M5.update();
    }
    
    delay(50);
  }
}

void showMenu() {
  Serial.println("Showing menu...");
  resetScreenPower();
  
  int selectedOption = 0;
  // Skip SEND QUEUE if empty, start at WIFI
  if (queueCount == 0) {
    selectedOption = 1;
  }
  int numOptions = 4;  // SEND QUEUE, CONNECT WIFI, REC MODE, BACK
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    // Only redraw when needed (not every loop iteration)
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // Title
      drawTextSafe("MENU", 10, 20, 3, COLOR_WHITE);
      
      // Storage indicator (top right, small)
      size_t totalBytes = SPIFFS.totalBytes();
      size_t usedBytes = SPIFFS.usedBytes();
      int usedPercent = (usedBytes * 100) / totalBytes;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(70, 22);
      if (usedPercent > 80) {
        M5.Display.setTextColor(COLOR_RED);
      } else if (usedPercent > 60) {
        M5.Display.setTextColor(COLOR_YELLOW);
      } else {
        M5.Display.setTextColor(COLOR_GRAY);
      }
      M5.Display.printf("%d%%", usedPercent);
      
      // Option 1: Send Queue
      int y = 65;
      // Caret (size 1)
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 3);
      M5.Display.setTextColor((selectedOption == 0 && queueCount > 0) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      // Text (size 1, more compact)
      M5.Display.setTextSize(1);
      M5.Display.setCursor(10, y + 3);
      if (queueCount > 0) {
        M5.Display.setTextColor((selectedOption == 0) ? COLOR_GREEN : COLOR_WHITE);
        M5.Display.printf("SEND %d", queueCount);
      } else {
        M5.Display.setTextColor(COLOR_GRAY);
        M5.Display.print("SEND -");
      }
      
      // Option 2: Connect WiFi
      y += 18;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 3);
      M5.Display.setTextColor((selectedOption == 1) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      M5.Display.setCursor(10, y + 3);
      M5.Display.setTextColor((selectedOption == 1) ? COLOR_GREEN : COLOR_WHITE);
      M5.Display.print("WIFI");
      
      // Option 3: Recording Mode Toggle
      y += 18;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 3);
      M5.Display.setTextColor((selectedOption == 2) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      M5.Display.setCursor(10, y + 3);
      M5.Display.setTextColor((selectedOption == 2) ? COLOR_GREEN : COLOR_WHITE);
      M5.Display.printf("REC: %ds", isLongRecordingMode ? RECORDING_TIME_LONG : RECORDING_TIME_NORMAL);
      
      // Option 4: Back
      y += 18;
      M5.Display.setTextSize(1);
      M5.Display.setCursor(2, y + 3);
      M5.Display.setTextColor((selectedOption == 3) ? COLOR_GREEN : COLOR_BG_PRIMARY);
      M5.Display.print(">");
      M5.Display.setCursor(10, y + 3);
      M5.Display.setTextColor((selectedOption == 3) ? COLOR_GREEN : COLOR_WHITE);
      M5.Display.print("BACK");
      
      // Button hints
      drawButtonHint("SELECT", COLOR_GREEN, 210, "A");
      drawButtonHint("NEXT", COLOR_RED, 225, "B");
      
      needsRedraw = false;
    }
    
    // Button A: Select option
    if (M5.BtnA.wasPressed()) {
      Serial.printf("Menu: Selected option %d\n", selectedOption);
      delay(200);
      M5.update();
      
      if (selectedOption == 0) {
        // Send queue - only if we have items
        if (queueCount > 0) {
          sendQueue();
          return;
        }
        // If queue empty, do nothing (shouldn't be selected anyway)
      } else if (selectedOption == 1) {
        // Connect WiFi - show WiFi network selection menu
        showWiFiMenu();
        needsRedraw = true;  // Redraw menu after returning from WiFi menu
      } else if (selectedOption == 2) {
        // Toggle recording mode
        isLongRecordingMode = !isLongRecordingMode;
        Serial.printf("Recording mode: %ds\n", isLongRecordingMode ? RECORDING_TIME_LONG : RECORDING_TIME_NORMAL);
        needsRedraw = true;
      } else if (selectedOption == 3) {
        // Back - return to ready screen
        displayReady();
        return;
      }
    }
    
    // Button B: Next option
    if (M5.BtnB.wasPressed()) {
      // Skip over SEND option if queue is empty
      do {
        selectedOption = (selectedOption + 1) % numOptions;
      } while (selectedOption == 0 && queueCount == 0);
      
      Serial.printf("Menu: Changed to option %d\n", selectedOption);
      needsRedraw = true;
      delay(200);
      M5.update();
    }
    
    delay(50);
  }
}

// Debug Menu
void showDebugMenu() {
  Serial.println("\n=== DEBUG MENU ===");
  resetScreenPower();
  
  int selectedOption = 0;
  int numOptions = 5;  // Text Test, LED Test, Sprites, Device Info, Back
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // Title
      drawTextSafe("DEBUG", 10, 15, 2, COLOR_YELLOW);
      
      // Options
      const char* options[] = {"Text Test", "LED Test", "Sprites", "Device", "Back"};
      int y = 50;
      for (int i = 0; i < numOptions; i++) {
        // Draw caret
        M5.Display.setTextSize(1);
        M5.Display.setCursor(2, y + 2);
        M5.Display.setTextColor((i == selectedOption) ? COLOR_GREEN : COLOR_BG_PRIMARY);
        M5.Display.print(">");
        // Draw text
        drawTextSafe(options[i], 10, y, 1, (i == selectedOption) ? COLOR_GREEN : COLOR_WHITE);
        y += 15;  // Tighter spacing
      }
      
      // Button hints
      drawButtonHint("SELECT", COLOR_GREEN, 210, "A");
      drawButtonHint("NEXT", COLOR_RED, 225, "B");
      
      needsRedraw = false;
    }
    
    // Button A: Select
    if (M5.BtnA.wasPressed()) {
      delay(200);
      if (selectedOption == 0) {
        showTextTest();
      } else if (selectedOption == 1) {
        showLEDTest();
      } else if (selectedOption == 2) {
        showSpriteViewer();
      } else if (selectedOption == 3) {
        showDeviceInfo();
      } else if (selectedOption == 4) {
        return;  // Back
      }
      needsRedraw = true;
    }
    
    // Button B: Next
    if (M5.BtnB.wasPressed()) {
      selectedOption = (selectedOption + 1) % numOptions;
      needsRedraw = true;
      delay(200);
    }
    
    delay(50);
  }
}

// Text Size Test
void showTextTest() {
  Serial.println("\n=== TEXT SIZE TEST ===");
  resetScreenPower();
  
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  drawTextSafe("TEXT TEST", 5, 30, 2, COLOR_YELLOW);
  drawTextSafe("Check Serial", 5, 60, 1, COLOR_WHITE);
  drawButtonHint("OK", COLOR_GREEN, 210, "A");
  
  // Print text size info to Serial
  Serial.println("\n--- Common Text Elements ---");
  previewTextSize("RECORDING", 4);
  previewTextSize("REC", 4);
  previewTextSize("SENDING", 3);
  previewTextSize("SUCCESS", 3);
  previewTextSize("MENU", 3);
  
  Serial.println("\n--- Testing Safe Draw ---");
  Serial.println("Screen width: 135px");
  
  const char* testTexts[] = {"SHORT", "MEDIUM TEXT", "THIS IS VERY LONG"};
  for (int i = 0; i < 3; i++) {
    for (int size = 1; size <= 4; size++) {
      int width = getTextWidth(testTexts[i], size);
      bool fits = (width + 10 <= 135);
      Serial.printf("'%s' size %d: %dpx %s\n", testTexts[i], size, width, fits ? "✓" : "✗");
    }
  }
  
  Serial.println("\nPress A to continue...\n");
  
  while (true) {
    M5.update();
    if (M5.BtnA.wasPressed()) {
      delay(200);
      return;
    }
    delay(50);
  }
}

// LED Test
void showLEDTest() {
  Serial.println("\n=== LED TEST ===");
  resetScreenPower();
  
  int state = 0;  // 0=off, 1=on
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      drawTextSafe("LED TEST", 5, 20, 2, COLOR_YELLOW);
      
      M5.Display.setTextSize(1);
      M5.Display.setCursor(5, 60);
      M5.Display.setTextColor(COLOR_WHITE);
      M5.Display.printf("State: %s", state == 0 ? "OFF" : "ON");
      
      drawButtonHint("TOGGLE", COLOR_GREEN, 210, "A");
      drawButtonHint("BACK", COLOR_RED, 225, "B");
      
      // Control LED
      // Note: M5StickC Plus2 has a red LED on GPIO 19
      pinMode(19, OUTPUT);
      digitalWrite(19, state == 1 ? HIGH : LOW);
      
      Serial.printf("LED: %s\n", state == 0 ? "OFF" : "ON");
      
      needsRedraw = false;
    }
    
    if (M5.BtnA.wasPressed()) {
      state = 1 - state;  // Toggle
      needsRedraw = true;
      delay(200);
    }
    
    if (M5.BtnB.wasPressed()) {
      digitalWrite(19, LOW);  // Turn off on exit
      delay(200);
      return;
    }
    
    delay(50);
  }
}

// Sprite Viewer - scroll through all sprites
void showSpriteViewer() {
  Serial.println("\n=== SPRITE VIEWER ===");
  resetScreenPower();
  
  int currentSprite = 0;
  int numSprites = 2;  // Bailey, Gengar
  bool needsRedraw = true;
  
  while (true) {
    M5.update();
    
    if (needsRedraw) {
      M5.Display.fillScreen(COLOR_BG_PRIMARY);
      
      // Draw current sprite
      if (currentSprite == 0) {
        drawBailey();
        // Label
        M5.Display.setTextSize(1);
        M5.Display.setCursor(5, 5);
        M5.Display.setTextColor(COLOR_YELLOW, COLOR_BG_PRIMARY);
        M5.Display.print("Bailey");
      } else if (currentSprite == 1) {
        drawGengar(4, 35);
        // Label
        M5.Display.setTextSize(1);
        M5.Display.setCursor(5, 5);
        M5.Display.setTextColor(COLOR_YELLOW, COLOR_BG_PRIMARY);
        M5.Display.print("Gengar");
      }
      
      // Button hints at bottom
      M5.Display.setTextSize(1);
      M5.Display.setCursor(5, 225);
      M5.Display.setTextColor(COLOR_WHITE, COLOR_BG_PRIMARY);
      M5.Display.print("A:Next B:Exit");
      
      needsRedraw = false;
    }
    
    // Button A: Next sprite
    if (M5.BtnA.wasPressed()) {
      currentSprite = (currentSprite + 1) % numSprites;
      needsRedraw = true;
      delay(200);
    }
    
    // Button B: Exit
    if (M5.BtnB.wasPressed()) {
      delay(200);
      return;
    }
    
    delay(50);
  }
}

// Device Info
void showDeviceInfo() {
  Serial.println("\n=== DEVICE INFO ===");
  resetScreenPower();
  
  M5.Display.fillScreen(COLOR_BG_PRIMARY);
  drawTextSafe("INFO", 5, 10, 2, COLOR_YELLOW);
  
  // Device ID
  M5.Display.setTextSize(1);
  M5.Display.setCursor(2, 35);
  M5.Display.setTextColor(COLOR_WHITE);
  M5.Display.printf("ID: %s", DEVICE_ID);
  
  // CPU Freq
  M5.Display.setCursor(2, 50);
  M5.Display.printf("CPU: %dMHz", getCpuFrequencyMhz());
  
  // Storage
  size_t totalKB = SPIFFS.totalBytes() / 1024;
  size_t usedKB = SPIFFS.usedBytes() / 1024;
  M5.Display.setCursor(2, 65);
  M5.Display.printf("Storage: %dK/%dK", usedKB, totalKB);
  
  // WiFi MAC
  M5.Display.setCursor(2, 80);
  M5.Display.printf("MAC: %s", WiFi.macAddress().c_str());
  
  // Battery
  M5.Display.setCursor(2, 95);
  M5.Display.printf("Battery: %d%%", M5.Power.getBatteryLevel());
  
  // Queue
  M5.Display.setCursor(2, 110);
  M5.Display.printf("Queue: %d items", queueCount);
  
  drawButtonHint("OK", COLOR_GREEN, 210, "A");
  
  // Print to serial too
  Serial.println("\n--- Device Info ---");
  Serial.printf("Device ID: %s\n", DEVICE_ID);
  Serial.printf("Username: %s\n", USERNAME);
  Serial.printf("CPU: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Storage: %d KB / %d KB\n", usedKB, totalKB);
  Serial.printf("WiFi MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("Battery: %d%%\n", M5.Power.getBatteryLevel());
  Serial.printf("Queue: %d items\n", queueCount);
  Serial.println();
  
  while (true) {
    M5.update();
    if (M5.BtnA.wasPressed()) {
      delay(200);
      return;
    }
    delay(50);
  }
}
