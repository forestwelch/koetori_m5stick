// Only dims, never turns off completely
void manageScreenPowerNoButton() {
  uint32_t elapsed = millis() - screenStateStartTime;
  
  if (elapsed >= SCREEN_DIM_DELAY && !screenIsDimmed) {
    // Dim after 2 seconds (set once per cycle)
    M5.Display.setBrightness(SCREEN_BRIGHTNESS_DIMMED);
    screenIsDimmed = true;
    Serial.println("Screen DIMMED");
  }
}

// Helper: Manage screen power state WITH button checking (for other screens)
// Dims after 2 seconds, A button resets brightness
void manageScreenPower() {
  // Check if A button pressed to wake screen
  M5.update();
  if (M5.BtnA.wasPressed()) {
    resetScreenPower();
    return;
  }
  
  // Call the no-button version to do the actual work
  manageScreenPowerNoButton();
}

// Helper: Reset screen to full brightness
void resetScreenPower() {
  if (screenIsOff) {
    M5.Display.wakeup();
    screenIsOff = false;
  }
  M5.Display.setBrightness(SCREEN_BRIGHTNESS);
  screenIsDimmed = false;
  screenStateStartTime = millis();
}

// Helper: Turn screen completely OFF (legacy)
void screenOff() {
  M5.Display.setBrightness(0);
  M5.Display.sleep();
  screenIsOff = true;
  Serial.println("Screen OFF");
}

// Helper: Turn screen back ON (legacy)
void screenOn() {
  if (screenIsOff) {
    M5.Display.wakeup();
    M5.Display.setBrightness(SCREEN_BRIGHTNESS);
    screenIsOff = false;
    screenIsDimmed = false;
    lastInteractionTime = millis();  // Reset idle timer
    displayReady();  // Redraw the screen
    Serial.println("Screen ON");
  }
}

// Load queue count from SPIFFS
int loadQueueCount() {
  File file = SPIFFS.open("/queue_count.txt", FILE_READ);
  if (!file) return 0;
  
  int count = file.parseInt();
  file.close();
  return count;
}

// Save queue count to SPIFFS
void saveQueueCount(int count) {
  File file = SPIFFS.open("/queue_count.txt", FILE_WRITE);
  if (file) {
    file.println(count);
    file.close();
  }
}

// Load last successful network from SPIFFS
int loadLastNetwork() {
  File file = SPIFFS.open("/last_network.txt", FILE_READ);
  if (!file) return -1;
  
  int network = file.parseInt();
  file.close();
  return network;
}

// Save last successful network to SPIFFS
void saveLastNetwork(int networkIndex) {
  File file = SPIFFS.open("/last_network.txt", FILE_WRITE);
  if (file) {
    file.println(networkIndex);
    file.close();
  }
}

// Add current recording to queue
bool addToQueue() {
  if (queueCount >= 5) {  // Max 5 queued recordings
    Serial.println("Queue full!");
    return false;
  }
  
  // Create filename with timestamp
  char queueFilename[48];
  unsigned long timestamp = millis() / 1000;  // Seconds since boot
  sprintf(queueFilename, "/q_%lu_%d.wav", timestamp, queueCount);
  
  // Copy /rec.wav to queue
  File src = SPIFFS.open("/rec.wav", FILE_READ);
  File dst = SPIFFS.open(queueFilename, FILE_WRITE);
  
  if (!src || !dst) {
    Serial.println("Failed to queue recording");
    if (src) src.close();
    if (dst) dst.close();
    return false;
  }
  
  // Copy file
  uint8_t buf[512];
  while (src.available()) {
    size_t len = src.read(buf, sizeof(buf));
    dst.write(buf, len);
  }
  
  src.close();
  dst.close();
  
  queueCount++;
  saveQueueCount(queueCount);
  Serial.printf("Added to queue (%d total)\n", queueCount);
  return true;
}


void writeWAVHeader(File file, uint32_t dataSize) {
  uint32_t fileSize = dataSize + 44 - 8;
  uint32_t byteRate = SAMPLE_RATE * 2;
  
  file.write((uint8_t*)"RIFF", 4);
  file.write((uint8_t*)&fileSize, 4);
  file.write((uint8_t*)"WAVE", 4);
  file.write((uint8_t*)"fmt ", 4);
  
  uint32_t fmtSize = 16;
  file.write((uint8_t*)&fmtSize, 4);
  uint16_t audioFormat = 1;
  file.write((uint8_t*)&audioFormat, 2);
  uint16_t numChannels = 1;
  file.write((uint8_t*)&numChannels, 2);
  uint32_t sampleRate = SAMPLE_RATE;
  file.write((uint8_t*)&sampleRate, 4);
  file.write((uint8_t*)&byteRate, 4);
  uint16_t blockAlign = 2;
  file.write((uint8_t*)&blockAlign, 2);
  uint16_t bitsPerSample = 16;
  file.write((uint8_t*)&bitsPerSample, 2);
  
  file.write((uint8_t*)"data", 4);
  file.write((uint8_t*)&dataSize, 4);
}
