# Ski Clock Neo

Arduino project for a NeoPixel LED matrix clock display.

## What is this?

This is an Arduino sketch (.ino file) designed to run on Arduino hardware with a WS2812 NeoPixel LED matrix. The code displays numbers using a custom 5x7 pixel font with smooth 2x scaling.

## Hardware Setup

- **Microcontroller**: ESP32 or ESP8266 (for WiFi support)
- **LED Matrix**: 16x16 WS2812 NeoPixels
- **Data Pin**: Pin 4

## Dependencies

- **[Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)** - LED matrix control
- **[AutoConnect](https://github.com/Hieromon/AutoConnect)** - WiFi management library
  - Install via Arduino IDE: Library Manager → Search "AutoConnect"
  - Or PlatformIO: `hieromon/AutoConnect@^1.4.2`
- ESP32/ESP8266 WiFi libraries (included with board package)

## Current Functionality

The sketch currently:
- **Advanced WiFi Management** (AutoConnect library):
  - Multiple network credential storage
  - Auto-fallback between saved networks
  - Portal always available for network switching
  - Background auto-reconnect on dropouts
- Cycles through digits 0-9 
- Updates every 2 seconds
- Displays in red color
- Centers text on the 16x16 matrix
- Uses 2x scaled font with diagonal smoothing for better appearance
- Ready for OTA (Over-The-Air) updates

## Code Structure

The project is cleanly organized into modular components:
- **ski-clock-neo.ino**: Main sketch with constants, setup(), and loop() only
- **font_5x7.h**: Font definitions for all displayable characters (0-9, :, -, ., °, C)
- **neopixel_render.h**: All NeoPixel rendering functions including character mapping, coordinate transformation, glyph drawing, and 2x diagonal smoothing
- **wifi_config.h**: WiFi management, captive portal web server, credential storage, and network utilities

This clean separation makes the code highly maintainable and easy to understand at a glance.

## WiFi Setup & Management

### First Time Setup
On first boot, the device creates a WiFi access point:
- **SSID**: `SkiClock-Setup`
- **Password**: `configure`

Connect to this network and the configuration portal will open automatically. Select your WiFi network, enter the password, and save.

### Advanced Features
- **Multiple Networks**: Add multiple WiFi networks - device tries them in order of signal strength
- **Always Accessible**: Portal remains available even when connected to WiFi
  - Simply connect to `SkiClock-Setup` anytime to manage networks
- **Auto-Reconnect**: Handles temporary WiFi dropouts with background retry (5-second interval)
- **Network Roaming**: Automatically switches to the strongest available saved network

### Network Management
To add or switch networks:
1. Connect to `SkiClock-Setup` WiFi network
2. Portal opens automatically (or go to 192.168.4.1)
3. Add new networks or remove old ones
4. Device saves all changes automatically

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
