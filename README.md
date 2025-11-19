# Ski Clock Neo

Arduino project for a NeoPixel LED matrix clock display.

## What is this?

This is an Arduino sketch (.ino file) designed to run on Arduino hardware with a WS2812 NeoPixel LED matrix. The code displays numbers using a custom 5x7 pixel font with smooth 2x scaling.

## Hardware Setup

- **Microcontroller**: Arduino (any board compatible with Adafruit_NeoPixel)
- **LED Matrix**: 16x16 WS2812 NeoPixels
- **Data Pin**: Pin 4

## Dependencies

- [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) library

## Current Functionality

The sketch currently:
- Cycles through digits 0-9 
- Updates every 2 seconds
- Displays in red color
- Centers text on the 16x16 matrix

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
