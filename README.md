# Ski Clock Neo

Arduino project for a NeoPixel LED matrix clock display.

## What is this?

This is an Arduino sketch (.ino file) designed to run on Arduino hardware with a WS2812 NeoPixel LED matrix. The code displays numbers using a custom 5x7 pixel font with smooth 2x scaling.

## Hardware Setup

- **Microcontroller**: ESP32 or ESP8266 (for WiFi support)
- **LED Matrix**: 16x16 WS2812 NeoPixels
- **Data Pin**: Pin 4

## Dependencies

- [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) library
- ESP32/ESP8266 WiFi libraries (included with board package)
- DNSServer library (included with ESP board package)

## Current Functionality

The sketch currently:
- **WiFi Configuration Portal** - Easy setup via captive portal web interface
- Cycles through digits 0-9 
- Updates every 2 seconds
- Displays in red color
- Centers text on the 16x16 matrix
- Uses 2x scaled font with diagonal smoothing for better appearance
- Stores WiFi credentials persistently
- Ready for OTA (Over-The-Air) updates

## Code Structure

The project is cleanly organized into modular components:
- **ski-clock-neo.ino**: Main sketch with constants, setup(), and loop() only
- **font_5x7.h**: Font definitions for all displayable characters (0-9, :, -, ., °, C)
- **neopixel_render.h**: All NeoPixel rendering functions including character mapping, coordinate transformation, glyph drawing, and 2x diagonal smoothing
- **wifi_config.h**: WiFi management, captive portal web server, credential storage, and network utilities

This clean separation makes the code highly maintainable and easy to understand at a glance.

## WiFi Setup

On first boot, the device creates a WiFi access point:
- **SSID**: `SkiClock-Setup`
- **Password**: `configure`

Connect to this network and the configuration portal will open automatically. Select your home WiFi network, enter the password, and the device will save the credentials and connect automatically on future boots.

## Running This Code

**Note**: This code cannot run in the Replit browser environment as it requires physical Arduino hardware.

To use this code:
1. Download the `.ino` file
2. Open it in Arduino IDE or PlatformIO
3. Install the Adafruit_NeoPixel library
4. Connect your hardware
5. Upload to your Arduino board

## License

See repository for license information.
