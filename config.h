/*
 * Configuration constants for Koetori M5StickC PLUS2
 */

#ifndef CONFIG_H
#define CONFIG_H

// BLE configuration (stream to iOS app; iPhone handles API upload)
#define BLE_DEVICE_NAME_PREFIX "Koetori-M5"
#define BLE_SERVICE_UUID         "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_AUDIO_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_CONTROL_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define BLE_CHAR_STATUS_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define BLE_AUDIO_CHUNK_SIZE     512   // MTU-safe; 2 bytes index + 510 bytes audio
#define BLE_AUDIO_PAYLOAD_SIZE   510   // per chunk

// Audio configuration
#define SAMPLE_RATE 16000
#define RECORDING_TIME_NORMAL 30  // 30 seconds (uses RAM buffer)
#define RECORDING_TIME_LONG 42    // 42 seconds (streams to SPIFFS)
#define MAX_RECORDING_SIZE (SAMPLE_RATE * 2 * RECORDING_TIME_NORMAL)

// Color palette
#define COLOR_BG_PRIMARY 0x0000    // Pure black
#define COLOR_WHITE 0xffde         // White
#define COLOR_RED 0xc10e           // Red
#define COLOR_GREEN 0x1594         // Green
#define COLOR_YELLOW 0xe620        // Yellow
#define COLOR_GRAY 0x5AEB          // Gray (disabled)

// Screen settings
#define SCREEN_BRIGHTNESS 50        // Normal brightness (0-255)
#define SCREEN_BRIGHTNESS_DIMMED 1  // Dimmed to save power (0-255)
#define SCREEN_DIM_DELAY 2000       // Dim after 2 seconds
#define IDLE_SCREEN_OFF_DELAY 10000 // Turn off after 10 seconds on ready screen (idle only)

#endif // CONFIG_H

