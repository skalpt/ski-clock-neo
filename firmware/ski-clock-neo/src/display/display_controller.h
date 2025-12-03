#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include <Arduino.h>    // Include Arduino core for uint8_t, etc.

// Display mode state machine
enum DisplayMode {
  MODE_NORMAL,          // Alternates between time and date every 4 seconds
  MODE_COUNTDOWN,       // Countdown 3, 2, 1 before timer starts
  MODE_TIMER,           // Timer running (00:00, 00:01, ...)
  MODE_FLASHING_RESULT, // Elapsed time flashing (0.5s interval for 8 seconds)
  MODE_DISPLAY_RESULT   // Elapsed time solid for 1 minute, then auto-revert
};

// Initialize display controller
// Initializes time/temperature data libraries at the end
// Display shows immediately, then data libraries init in background
void initDisplayController();

// Set display mode (normal or timer mode)
void setDisplayMode(DisplayMode mode);

// Get current display mode
DisplayMode getDisplayMode();

// Force immediate update of all display rows
// Useful after WiFi reconnection or manual refresh
void forceDisplayUpdate();

// Called by data_temperature when temperature value changes
// Updates row 1 with new temperature reading
void updateTemperatureDisplay();

#endif
