#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include <Arduino.h>

// Display mode for row 0
enum DisplayMode {
  MODE_NORMAL,    // Alternates between time and date every 4 seconds
  MODE_TIMER      // Reserved for future timer feature (time/date/temp rotation)
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

#endif
