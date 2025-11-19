# Ski Clock Neo - Arduino NeoPixel Project

## Overview
This is an Arduino project for a ski clock display using WS2812 NeoPixel LED matrices. The code displays numbers on a 16x16 LED matrix with custom font rendering.

**Important**: This is embedded hardware code that requires physical Arduino hardware and cannot run directly in the Replit environment.

## Hardware Requirements
- **ESP32 or ESP8266** board (for WiFi support)
- WS2812 NeoPixel LED matrix (16x16)
- Data pin connected to pin 4

## Features
- Custom 5x7 pixel font for digits 0-9 and special characters (-,., °, C, :)
- 2x scaling with diagonal smoothing for better appearance
- Serpentine wiring support for LED matrices
- **WiFi configuration portal** - Easy setup through captive portal web interface
- Persistent WiFi credential storage
- OTA update ready
- Currently displays counting digits 0-9 in red

## Project Structure
- `ski-clock-neo.ino` - Main sketch (constants, setup, loop)
- `font_5x7.h` - Font data definitions (glyphs, widths, constants)
- `neopixel_render.h` - All NeoPixel rendering functions (character mapping, scaling, drawing)
- `wifi_config.h` - WiFi management, captive portal, and web configuration interface
- `info.sh` - Information script for Replit environment

## Libraries Required
- Adafruit_NeoPixel
- ESP32/ESP8266 WiFi libraries (built-in)
- DNSServer (built-in with ESP boards)

## How to Use
This code needs to be uploaded to an ESP32 or ESP8266 board using the Arduino IDE or PlatformIO:

### First Time Setup
1. Install the Adafruit_NeoPixel library
2. Select ESP32 or ESP8266 board in Arduino IDE
3. Connect your NeoPixel matrix to pin 4
4. Upload the sketch to your board

### WiFi Configuration
1. On first boot (or if no WiFi is configured), the device creates an access point:
   - **SSID:** `SkiClock-Setup`
   - **Password:** `configure`
2. Connect to this network with your phone/computer
3. A captive portal will open automatically (or navigate to `192.168.4.1`)
4. Select your WiFi network and enter the password
5. The device will save credentials and restart
6. Future boots will connect automatically to your WiFi

### Normal Operation
- The display cycles through digits 0-9 every 2 seconds
- WiFi credentials are stored permanently
- Ready for OTA updates

## Development in Replit
Since this is Arduino hardware code, you can use this Replit to:
- View and edit the code
- Share the code with others
- Version control your changes
- Add documentation

To actually run this code, you'll need to use Arduino IDE or PlatformIO on a computer connected to Arduino hardware.

## Code Organization
The code has been refactored for excellent maintainability:
- **Clean separation**: Main .ino contains only constants, setup(), and loop()
- **Modular headers**: Font data in `font_5x7.h`, all rendering in `neopixel_render.h`
- **Memory efficient**: Uses static buffer instead of stack allocation for 2x scaling (~140 bytes saved per glyph render)
- **Extended character support**: Recognizes actual degree symbol (0xB0) in addition to '*' placeholder

## Recent Changes
- 2025-11-19: Added WiFi support with captive portal configuration interface, persistent credential storage, preparation for OTA updates
- 2025-11-19: Refactored code - extracted font data to font_5x7.h, all rendering to neopixel_render.h, improved memory efficiency, added degree symbol support
- 2025-11-19: Initial import from GitHub
