// ============================================================================
// display_controller.cpp - Display content scheduling and mode management
// ============================================================================
// This library manages what content is shown on the display. It handles:
// - Time/date alternation on row 0 (toggles every 4 seconds)
// - Temperature display on row 1
// - Time change detection for immediate updates
// - Display mode switching (normal vs timer modes)
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "display_controller.h"
#include "display_core.h"
#include "data_time.h"
#include "data_temperature.h"
#include "timing_helpers.h"
#include "debug.h"

// ============================================================================
// STATE VARIABLES
// ============================================================================

static DisplayMode currentMode = MODE_NORMAL;        // Current display mode
static volatile bool showingTime = true;             // Toggle: true = time, false = date

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void toggleTimerCallback();
void onTimeChange(uint8_t flags);
void updateRow0();
void updateRow1();

// ============================================================================
// TIMER CALLBACKS
// ============================================================================

// Timer callback for 4-second time/date toggle
void toggleTimerCallback() {
  showingTime = !showingTime;
  updateRow0();
}

// Callback for time changes - forces display update when minute or date changes
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

// ============================================================================
// ROW UPDATE HELPERS
// ============================================================================

// Update row 0 with time or date (depending on toggle state)
void updateRow0() {
  // Check if NTP is synced before attempting to display time/date
  if (!isTimeSynced()) {
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
        DEBUG_PRINTLN("Row 0: Date format failed");
      }
    }
  } else {
    // MODE_TIMER: Reserved for future implementation
    setText(0, "TIMER");
  }
}

// Update row 1 with temperature
void updateRow1() {
  char buffer[32];
  
  if (formatTemperature(buffer, sizeof(buffer))) {
    setText(1, buffer);
    DEBUG_PRINT("Row 1: Temp = ");
    DEBUG_PRINTLN(buffer);
  } else {
    setText(1, "~~*C");
    DEBUG_PRINTLN("Row 1: Temperature not available");
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initDisplayController() {
  DEBUG_PRINTLN("Initializing display controller");
  
  // Set initial state
  showingTime = true;
  forceDisplayUpdate();
  renderNow();  // Force immediate render (don't wait for next tick)
  
  // Create 4-second toggle timer using timing_helpers library
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

// ============================================================================
// PUBLIC API
// ============================================================================

// Set display mode (normal or timer)
void setDisplayMode(DisplayMode mode) {
  if (currentMode != mode) {
    currentMode = mode;
    DEBUG_PRINT("Display mode changed to: ");
    DEBUG_PRINTLN(mode == MODE_NORMAL ? "NORMAL" : "TIMER");
  }
}

// Get current display mode
DisplayMode getDisplayMode() {
  return currentMode;
}

// Force update of all display rows
void forceDisplayUpdate() {
  updateRow0();
  updateRow1();
}

// Called by data_temperature library when temperature value changes
void updateTemperatureDisplay() {
  updateRow1();
}
