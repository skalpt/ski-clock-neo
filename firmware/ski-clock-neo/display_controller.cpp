#include "display_controller.h"

#include "display_core.h"
#include "data_time.h"
#include "data_temperature.h"
#include "timing_helpers.h"
#include "debug.h"

// Controller state
static DisplayMode currentMode = MODE_NORMAL;
static volatile bool showingTime = true;  // Toggle: true = time, false = date (volatile for ESP8266 ISR safety)

// Forward declarations
void updateRow0();
void updateRow1();
void onTimeChange(uint8_t flags);

// Timer callback for 4-second time/date toggle
void toggleTimerCallback() {
  showingTime = !showingTime;
  updateRow0();
}

// Callback for time changes - forces display update
void onTimeChange(uint8_t flags) {
  DEBUG_PRINT("Time change detected, flags: ");
  DEBUG_PRINTLN(flags);
  
  // If minute changed and we're showing time, update immediately
  if ((flags & TIME_CHANGE_MINUTE) && showingTime) {
    DEBUG_PRINTLN("Forcing time display update");
    updateRow0();
  }
  
  // If date changed and we're showing date, update immediately
  if ((flags & TIME_CHANGE_DATE) && !showingTime) {
    DEBUG_PRINTLN("Forcing date display update");
    updateRow0();
  }
}

void updateRow0() {
  // Check if NTP is synced before attempting to display time/date
  if (!isTimeSynced()) {
    // NTP not synced yet, show placeholder
    setText(0, "~~.~~");
    DEBUG_PRINTLN("Row 0: Waiting for NTP sync");
    return;
  }
  
  char buffer[32];
  
  if (currentMode == MODE_NORMAL) {
    if (showingTime) {
      // Show time: "hh.mm"
      if (formatTime(buffer, sizeof(buffer))) {
        setText(0, buffer);
        DEBUG_PRINT("Row 0: Time = ");
        DEBUG_PRINTLN(buffer);
      } else {
        setText(0, "~~.~~");
        DEBUG_PRINTLN("Row 0: Time format failed");
      }
    } else {
      // Show date: "dd-mm"
      if (formatDate(buffer, sizeof(buffer))) {
        setText(0, buffer);
        DEBUG_PRINT("Row 0: Date = ");
        DEBUG_PRINTLN(buffer);
      } else {
        // Don't set text here - keep showing time if date format fails
        DEBUG_PRINTLN("Row 0: Date format failed");
      }
    }
  } else {
    // MODE_TIMER: Reserved for future implementation
    // Will rotate between time, date, and temperature
    setText(0, "TIMER");
  }
}

void updateRow1() {
  char buffer[32];
  
  // Row 1: Always shows temperature
  if (formatTemperature(buffer, sizeof(buffer))) {
    setText(1, buffer);
    DEBUG_PRINT("Row 1: Temp = ");
    DEBUG_PRINTLN(buffer);
  } else {
    setText(1, "~~*C");
    DEBUG_PRINTLN("Row 1: Temperature not available");
  }
}

void initDisplayController() {
  DEBUG_PRINTLN("Initializing display controller");
  
  showingTime = true;    // Start with time display
  forceDisplayUpdate();  // Force initial update
  renderNow();           // Force immediate render (don't wait for next tick)
  
  // Create 4-second toggle timer (uses timer_task library for platform abstraction)
  createTimer("DispToggle", 4000, toggleTimerCallback);
  
  DEBUG_PRINTLN("Display controller initialized");
  
  // Initialize data libraries LAST - allows display to show immediately during boot
  // Time library: NTP sync for Sweden timezone (CET/CEST)
  // Also starts 1-second time change detection timer internally
  initTimeData();
  
  // Register time change callback - forces display update when minute/date changes
  setTimeChangeCallback(onTimeChange);
  
  // Temperature library: DS18B20 sensor with automatic 30-second polling
  // Temperature library owns ALL temperature timing (tickers, callbacks)
  // Calls updateTemperatureDisplay() callback when value changes
  initTemperatureData();
}

void setDisplayMode(DisplayMode mode) {
  if (currentMode != mode) {
    currentMode = mode;
    DEBUG_PRINT("Display mode changed to: ");
    DEBUG_PRINTLN(mode == MODE_NORMAL ? "NORMAL" : "TIMER");
  }
}

DisplayMode getDisplayMode() {
  return currentMode;
}

void forceDisplayUpdate() {
  updateRow0();
  updateRow1();
}

void updateTemperatureDisplay() {
  // Called by data_temperature library when temperature changes
  updateRow1();
}
