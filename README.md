# Koetori M5StickC PLUS2 Firmware

Voice recording firmware for the M5StickC PLUS2 that records audio, uploads it to the Koetori API for transcription and categorization.

## Features

- 🎙️ High-quality audio recording (16kHz, 16-bit)
- 📡 WiFi management with multiple network support
- 🔄 Queue system for offline recordings
- ⚡ Power management (screen dimming, auto-off)
- 🎨 Custom boot and ready screen sprites (Bailey & Gengar)
- 🔧 Debug menu with device info, LED tests, and sprite viewer
- 📊 Enhanced upload feedback (category, tokens, quota, processing time)
- 🕐 Two recording modes: Normal (30s) and Long (42s)

## Hardware Requirements

- **M5StickC PLUS2** (ESP32-PICO-V3-02)
- **Built-in microphone** (PDM microphone on M5StickC PLUS2)
- **WiFi connection** for uploading recordings

## Software Requirements

- [Arduino IDE](https://www.arduino.cc/en/software) or [arduino-cli](https://arduino.github.io/arduino-cli/)
- [M5StickCPlus2 Library](https://github.com/m5stack/M5StickCPlus2)
- ESP32 Board Support Package

### Arduino IDE Setup

1. **Install ESP32 Board Support:**

   - Open Arduino IDE → Preferences
   - Add to "Additional Board Manager URLs":
     ```
     https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
     ```
   - Go to Tools → Board → Board Manager
   - Search for "M5Stack" and install "M5Stack by M5Stack official"

2. **Install M5StickCPlus2 Library:**

   - Go to Sketch → Include Library → Manage Libraries
   - Search for "M5StickCPlus2" and install

3. **Select Board:**
   - Tools → Board → M5Stack Arduino → M5StickC PLUS2

## Configuration

### 1. Copy Secrets File

```bash
cp secrets.h.example secrets.h
```

### 2. Edit `secrets.h` with your credentials:

```cpp
// WiFi networks (you can add multiple)
WiFiNetwork wifiNetworks[] = {
  {"YourWiFiSSID", "YourWiFiPassword"},
  {"AnotherNetwork", "AnotherPassword"},
};
const int numNetworks = 2;  // Update this count

// API configuration
const char* API_KEY = "your-api-key-here";        // From your Koetori instance
const char* DEVICE_ID = "m5stick-yourname-001";   // Unique device identifier
const char* USERNAME = "yourname";                // Your Koetori username
```

### 3. Get Your API Key

Your API key should come from your Koetori deployment. The API key is configured via the `DEVICE_API_KEY` environment variable in your Koetori instance.

## Upload to Device

### Using Arduino IDE:

1. Connect M5StickC PLUS2 via USB
2. Select correct COM port (Tools → Port)
3. Click "Upload" button

### Using arduino-cli:

```bash
arduino-cli compile --fqbn m5stack:esp32:m5stack_stickc_plus2 koetori_m5stick.ino
arduino-cli upload -p /dev/cu.usbserial-* --fqbn m5stack:esp32:m5stack_stickc_plus2 koetori_m5stick.ino
```

## Usage

### Button Layout

- **Button A (Green)** - Main action / Select
- **Button B (Red)** - Cancel / Menu / Scroll
- **Button C (Yellow/Power)** - Debug menu / Secondary actions

### Basic Operation

1. **Power On** - Shows Bailey sprite at boot
2. **Ready Screen** - Shows Gengar sprite with battery, WiFi status
3. **Press A** - Start recording (30s by default)
4. **Press A Again** - Stop recording early
5. **Press B** - Cancel recording
6. **Auto Upload** - If WiFi connected, uploads immediately
7. **Queue Mode** - If no WiFi, saves to queue

### Recording Modes

- **Normal Mode (30s)** - Uses RAM buffer, faster, default
- **Long Mode (42s)** - Streams to SPIFFS, for longer recordings
- Toggle in main menu → "Long Mode"

### Menu System

**Press B on ready screen** to open menu:

- **Send Queue** - Upload queued recordings
- **WiFi** - Connect to WiFi networks
- **Long Mode** - Toggle recording duration
- **Back** - Return to ready screen

### Debug Menu

**Press C (Power button)** to access debug tools:

- **Text Test** - Test text sizing (check serial output)
- **LED Test** - Test the RGB LED (toggle on/off)
- **Sprites** - View all sprites (Bailey, Gengar)
- **Device Info** - Show device ID, MAC, storage, battery, etc.

### WiFi Management

1. Press B → WiFi
2. Scroll through available networks with B
3. Press A to connect
4. Device remembers last successful network
5. Auto-connects on boot if previously connected

### Power Management

- Screen **dims after 2 seconds** of inactivity
- Screen **turns off after 10 seconds** on ready screen
- Press any button to wake
- WiFi uses sleep mode when idle to save power

## Success Screen Information

After successful upload, the device shows:

```
SENT ✓

Type: task
15.5s / 484KB
Tokens: 600
Quota: 4600 left
Time: 1.2s
```

This requires the enhanced API response. See `API_RESPONSE_ENHANCEMENTS.md` for details on updating your Koetori API.

## Troubleshooting

### "Storage Full" Error Immediately

- Your SPIFFS is too full for long recordings
- Try shorter recordings or clear queue
- Debug menu → Device Info shows storage usage

### Recording Cuts Off

- Long recordings are limited to 42s due to SPIFFS size
- Use Normal mode (30s) for reliable recordings
- Check available storage in Debug → Device Info

### WiFi Won't Connect

- Check credentials in `secrets.h`
- Make sure network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check serial monitor for connection errors

### Upload Fails

- Verify `API_KEY` in `secrets.h` matches your server
- Check `API_URL` points to correct server
- Ensure Koetori API is running and accessible
- Check serial monitor for HTTP error codes

### Serial Monitor Not Working

- Set baud rate to **115200**
- Make sure USB cable supports data (not just power)

## File Structure

```
koetori_m5stick/
├── koetori_m5stick.ino       # Main firmware
├── secrets.h                 # Your credentials (not committed)
├── secrets.h.example         # Template for secrets
├── gengar.txt                # Gengar sprite data source
├── converter.html            # Image to sprite converter
├── .gitignore                # Git ignore rules
├── README.md                 # This file
├── API_RESPONSE_ENHANCEMENTS.md  # API upgrade guide
└── RECENT_M5STICK_CHANGES.md     # Changelog
```

## Development

### Viewing Serial Output

```bash
screen /dev/cu.usbserial-* 115200
# or
arduino-cli monitor -p /dev/cu.usbserial-* -c baudrate=115200
```

### Converting Images to Sprites

1. Prepare 135x240 pixel image (or 128x128 for smaller sprites)
2. Open `converter.html` in browser
3. Upload image
4. Copy generated C array code
5. Paste into firmware

### Audio Format

- **Sample Rate:** 16kHz
- **Bit Depth:** 16-bit signed PCM
- **Channels:** Mono
- **Format:** WAV with 44-byte header
- **Gain:** 10x applied during recording

## API Integration

This firmware sends recordings to the Koetori API endpoint:

```
POST https://www.koetori.com/api/transcribe/device
```

**Headers:**

- `x-api-key: YOUR_API_KEY`

**Form Data:**

- `audio`: WAV file
- `device_id`: Device identifier
- `username`: Username

**Response:** See `API_RESPONSE_ENHANCEMENTS.md` for enhanced response format.

## Contributing

This is a companion project to [Koetori](https://github.com/yourusername/koetori).

For firmware improvements:

1. Fork the repository
2. Create a feature branch
3. Test on actual hardware
4. Submit a pull request

## License

MIT License - see LICENSE file for details

## Related Projects

- [Koetori](https://github.com/yourusername/koetori) - The main web application
- [M5StickC PLUS2](https://docs.m5stack.com/en/core/M5StickC%20PLUS2) - Hardware documentation

## Support

For issues related to:

- **Firmware bugs** - Open an issue in this repository
- **API issues** - Check the main Koetori repository
- **Hardware issues** - Consult M5Stack documentation

---

**Made with ❤️ for Koetori**


