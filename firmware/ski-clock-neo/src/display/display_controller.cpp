// ============================================================================
// display_controller.cpp - Display content scheduling and mode management
// ============================================================================
// This library manages what content is shown on the display. It handles:
// - Time/date alternation on row 0 (toggles every 4 seconds in normal mode)
// - Temperature display on row 1
// - Button-controlled timer mode with countdown and elapsed time
// - Time change detection for immediate updates
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "display_controller.h"       // This file's header
#include "display_core.h"             // Display rendering core
#include "../data/data_time.h"        // Time data provider
#include "../data/data_temperature.h" // Temperature data provider
#include "../data/data_button.h"      // Button input
#include "../core/event_log.h"        // For logging display events
#include "../core/timer_helpers.h"    // For unified tick timer
#include "../core/debug.h"            // For debug logging

// ============================================================================
// CONSTANTS
// ============================================================================

static const uint32_t TICK_INTERVAL_MS = 500;          // Unified tick interval (0.5 seconds)
static const uint8_t TICKS_PER_TOGGLE = 8;             // 8 ticks = 4 seconds for time/date toggle
static const uint8_t TICKS_PER_SECOND = 2;             // 2 ticks = 1 second for countdown/timer
static const uint8_t FLASH_TICKS = 16;                 // 16 ticks = 8 seconds of flashing
static const uint16_t RESULT_TICKS = 120;              // 120 ticks = 60 seconds result display

// ============================================================================
// STATE VARIABLES
// ============================================================================

static DisplayMode currentMode = MODE_NORMAL;          // Current display mode

// Unified tick counter (reset on mode changes for synchronization)
static volatile uint16_t tickCounter = 0;

// Normal mode toggle state (time vs date)
static volatile bool showingTime = true;

// Timer mode toggle state (cycles through 3 values: 0=time, 1=date, 2=temp)
static volatile uint8_t timerTopRowState = 0;

// Countdown state
static int8_t countdownValue = 3;                      // Countdown: 3, 2, 1

// Timer state
static uint32_t elapsedSeconds = 0;                    // Elapsed time in seconds

// Flash state
static bool flashVisible = true;                       // Toggle for flashing display

#if ACTIVITY_PIXEL_ENABLED
// Activity pixel state (blinks every second)
static bool activityPixelState = false;
#endif

// Transition guard to prevent rapid state changes
static volatile bool transitionInProgress = false;
static uint32_t lastTransitionTime = 0;
static const uint32_t TRANSITION_LOCKOUT_MS = 200;     // Minimum time between transitions

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void unifiedTickCallback();
void buttonPollCallback();
void onTimeChange(uint8_t flags);
bool updateRow0Content();
bool updateRow1Content();
void updateBothRows();
void startCountdown();
void startTimer();
void startFlashingResult();
void startDisplayResult();
void returnToNormal();
void cancelTimer();
void onButtonPress();

// ============================================================================
// TIMER CALLBACKS
// ============================================================================

// Unified tick callback - handles all display timing from a single 500ms timer
void unifiedTickCallback() {
  // Increment tick counter first
  tickCounter++;
  bool needsUpdate = false;
  
  #if ACTIVITY_PIXEL_ENABLED
  // Toggle activity pixel every 2 ticks (1 second)
  if (tickCounter % TICKS_PER_SECOND == 0) {
    activityPixelState = !activityPixelState;
    setActivityPixelVisible(activityPixelState);
    triggerRender();  // Force render even if text content unchanged
  }
  #endif
  
  // Capture current mode to prevent race conditions during state transitions
  DisplayMode mode = currentMode;
  
  switch (mode) {
    case MODE_NORMAL:
      // Toggle time/date every 8 ticks (4 seconds)
      if (tickCounter % TICKS_PER_TOGGLE == 0) {
        showingTime = !showingTime;
        needsUpdate = true;
      }
      break;
      
    case MODE_COUNTDOWN:
      // Toggle top row every 8 ticks (4 seconds)
      if (tickCounter % TICKS_PER_TOGGLE == 0) {
        timerTopRowState = (timerTopRowState + 1) % 3;
        needsUpdate = true;
      }
      // Decrement countdown every 2 ticks (1 second), but not on first tick
      if (tickCounter > 1 && tickCounter % TICKS_PER_SECOND == 0) {
        countdownValue--;
        if (countdownValue <= 0) {
          startTimer();
          return;  // Exit immediately - startTimer handles its own update
        }
        needsUpdate = true;  // Only update when countdown actually changes
      }
      break;
      
    case MODE_TIMER:
      // Toggle top row every 8 ticks (4 seconds)
      if (tickCounter % TICKS_PER_TOGGLE == 0) {
        timerTopRowState = (timerTopRowState + 1) % 3;
        needsUpdate = true;
      }
      // Increment elapsed time every 2 ticks (1 second), but not on first tick
      if (tickCounter > 1 && tickCounter % TICKS_PER_SECOND == 0) {
        elapsedSeconds++;
        // Max timer is 99:59 (5999 seconds) - return to normal at 100 minutes
        if (elapsedSeconds >= 6000) {
          returnToNormal();
          return;
        }
        needsUpdate = true;  // Only update when elapsed time actually changes
      }
      break;
      
    case MODE_FLASHING_RESULT:
      // Toggle top row every 8 ticks (4 seconds)
      if (tickCounter % TICKS_PER_TOGGLE == 0) {
        timerTopRowState = (timerTopRowState + 1) % 3;
        needsUpdate = true;
      }
      // Toggle flash visibility every tick (0.5 seconds)
      flashVisible = !flashVisible;
      needsUpdate = true;
      // Check if flash duration exceeded (16 ticks = 8 seconds)
      if (tickCounter >= FLASH_TICKS) {
        startDisplayResult();
        return;  // Exit immediately - startDisplayResult handles its own update
      }
      break;
      
    case MODE_DISPLAY_RESULT:
      // Toggle top row every 8 ticks (4 seconds)
      if (tickCounter % TICKS_PER_TOGGLE == 0) {
        timerTopRowState = (timerTopRowState + 1) % 3;
        needsUpdate = true;
      }
      // Update display every 2 ticks (1 second) to keep clock current
      if (tickCounter % TICKS_PER_SECOND == 0) {
        needsUpdate = true;
      }
      // Check if result display time exceeded (120 ticks = 60 seconds)
      if (tickCounter >= RESULT_TICKS) {
        returnToNormal();
        return;  // Exit immediately - returnToNormal handles its own update
      }
      break;
  }
  
  if (needsUpdate) {
    updateBothRows();
  }
}

// Button poll callback: check for button state changes
void buttonPollCallback() {
  updateButton();
}

// Callback for time changes - forces display update when minute or date changes
void onTimeChange(uint8_t flags) {
  DEBUG_PRINT("Time change detected, flags: ");
  DEBUG_PRINTLN(flags);
  
  if (currentMode == MODE_NORMAL) {
    if ((flags & TIME_CHANGE_MINUTE) && showingTime) {
      DEBUG_PRINTLN("Forcing time display update");
      updateBothRows();
    }
    if ((flags & TIME_CHANGE_DATE) && !showingTime) {
      DEBUG_PRINTLN("Forcing date display update");
      updateBothRows();
    }
  } else if (currentMode == MODE_FLASHING_RESULT || currentMode == MODE_DISPLAY_RESULT) {
    // Result modes show current time on top row - update on minute change
    if (flags & TIME_CHANGE_MINUTE) {
      DEBUG_PRINTLN("Forcing time display update in result mode");
      updateBothRows();
    }
  }
}

// ============================================================================
// ROW UPDATE HELPERS
// ============================================================================

// Set row 0 content based on current mode (does not trigger render)
// Returns true if content actually changed
bool updateRow0Content() {
  static char buffer[32];
  
  if (currentMode == MODE_NORMAL) {
    // Normal mode: alternate time/date
    if (!isTimeSynced()) {
      return setTextNoRender(0, "~~.~~");
    }
    
    if (showingTime) {
      if (formatTime(buffer, sizeof(buffer))) {
        return setTextNoRender(0, buffer);
      } else {
        return setTextNoRender(0, "~~.~~");
      }
    } else {
      if (formatDate(buffer, sizeof(buffer))) {
        return setTextNoRender(0, buffer);
      }
    }
  } else if (currentMode == MODE_COUNTDOWN || currentMode == MODE_TIMER) {
    // Timer modes: cycle through time/date/temp on top row
    if (!isTimeSynced()) {
      return setTextNoRender(0, "~~.~~");
    }
    
    switch (timerTopRowState) {
      case 0: // Time
        if (formatTime(buffer, sizeof(buffer))) {
          return setTextNoRender(0, buffer);
        } else {
          return setTextNoRender(0, "~~.~~");
        }
        break;
      case 1: // Date
        if (formatDate(buffer, sizeof(buffer))) {
          return setTextNoRender(0, buffer);
        }
        break;
      case 2: // Temperature
        if (formatTemperature(buffer, sizeof(buffer))) {
          return setTextNoRender(0, buffer);
        } else {
          return setTextNoRender(0, "~~*C");
        }
        break;
    }
  } else if (currentMode == MODE_FLASHING_RESULT || currentMode == MODE_DISPLAY_RESULT) {
    // Result modes: cycle through time/date/temp on top row (same as timer modes)
    if (!isTimeSynced()) {
      return setTextNoRender(0, "~~.~~");
    }
    
    switch (timerTopRowState) {
      case 0: // Time
        if (formatTime(buffer, sizeof(buffer))) {
          return setTextNoRender(0, buffer);
        } else {
          return setTextNoRender(0, "~~.~~");
        }
        break;
      case 1: // Date
        if (formatDate(buffer, sizeof(buffer))) {
          return setTextNoRender(0, buffer);
        }
        break;
      case 2: // Temperature
        if (formatTemperature(buffer, sizeof(buffer))) {
          return setTextNoRender(0, buffer);
        } else {
          return setTextNoRender(0, "~~*C");
        }
        break;
    }
  }
  return false;
}

// Set row 1 content based on current mode (does not trigger render)
// Returns true if content actually changed
bool updateRow1Content() {
  static char buffer[32];
  
  if (currentMode == MODE_NORMAL) {
    // Normal mode: show temperature
    if (formatTemperature(buffer, sizeof(buffer))) {
      return setTextNoRender(1, buffer);
    } else {
      return setTextNoRender(1, "~~*C");
    }
  } else if (currentMode == MODE_COUNTDOWN) {
    // Countdown mode: show countdown number
    snprintf(buffer, sizeof(buffer), "   %d", countdownValue);
    return setTextNoRender(1, buffer);
  } else if (currentMode == MODE_TIMER) {
    // Timer mode: show elapsed time as MM:SS
    uint32_t mins = elapsedSeconds / 60;
    uint32_t secs = elapsedSeconds % 60;
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu", mins, secs);
    return setTextNoRender(1, buffer);
  } else if (currentMode == MODE_FLASHING_RESULT) {
    // Flashing result: show elapsed time or blank
    if (flashVisible) {
      uint32_t mins = elapsedSeconds / 60;
      uint32_t secs = elapsedSeconds % 60;
      snprintf(buffer, sizeof(buffer), "%02lu:%02lu", mins, secs);
      return setTextNoRender(1, buffer);
    } else {
      return setTextNoRender(1, "     ");
    }
  } else if (currentMode == MODE_DISPLAY_RESULT) {
    // Display result: show elapsed time solid
    uint32_t mins = elapsedSeconds / 60;
    uint32_t secs = elapsedSeconds % 60;
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu", mins, secs);
    return setTextNoRender(1, buffer);
  }
  return false;
}

// Update both rows atomically (set content, then trigger single render if changed)
void updateBothRows() {
  bool row0Changed = updateRow0Content();
  bool row1Changed = updateRow1Content();
  
  // Only trigger render if something actually changed
  if (row0Changed || row1Changed) {
    triggerRender();
  }
}

// ============================================================================
// STATE TRANSITIONS
// ============================================================================

void startCountdown() {
  DEBUG_PRINTLN("Starting countdown mode");
  currentMode = MODE_COUNTDOWN;
  countdownValue = 3;
  timerTopRowState = 0;
  tickCounter = 0;  // Reset tick counter for synchronization
  
  logEvent("button_press", "{\"action\":\"timer_start\"}");
  
  updateBothRows();
  
  // Restart tick timer to sync phase - ensures immediate "3" display
  // and subsequent ticks are aligned from this moment
  restartTimer("DisplayTick");
}

void startTimer() {
  DEBUG_PRINTLN("Starting timer mode");
  currentMode = MODE_TIMER;
  elapsedSeconds = 0;
  timerTopRowState = 0;  // Reset to show time first
  tickCounter = 0;  // Reset tick counter for synchronization
  
  updateBothRows();
  // NOTE: Do NOT call restartTimer() here - this is called from within
  // the timer callback when countdown reaches 0. Restarting the timer
  // from inside its own callback causes a crash (use-after-free).
}

void startFlashingResult() {
  DEBUG_PRINTLN("Starting flashing result mode");
  currentMode = MODE_FLASHING_RESULT;
  flashVisible = true;
  tickCounter = 0;  // Reset tick counter for flash duration tracking

  logEvent("button_press", "{\"action\":\"timer_stop\"}");

  updateBothRows();
  // NOTE: restartTimer() is called by onButtonPress() after this returns,
  // since this function is only called from button press context.
}

void startDisplayResult() {
  DEBUG_PRINTLN("Starting display result mode");
  currentMode = MODE_DISPLAY_RESULT;
  tickCounter = 0;  // Reset tick counter for result timeout tracking

  updateBothRows();
  // NOTE: Do NOT call restartTimer() here - this is called from within
  // the timer callback when flash period ends. Restarting the timer
  // from inside its own callback causes a crash.
}

void returnToNormal() {
  DEBUG_PRINTLN("Returning to normal mode");
  currentMode = MODE_NORMAL;
  showingTime = true;
  tickCounter = 0;  // Reset tick counter for synchronization

  // Reset timer state for next use
  timerTopRowState = 0;
  elapsedSeconds = 0;
  countdownValue = 3;
  flashVisible = true;

  logEvent("display_mode_change", "{\"from\":\"timer\",\"to\":\"normal\"}");

  updateBothRows();
  // NOTE: Do NOT call restartTimer() here - this may be called from within
  // the timer callback (result timeout) or from cancelTimer (button press).
  // The button press path handles restart separately.
}

void cancelTimer() {
  DEBUG_PRINTLN("Timer cancelled");
  logEvent("button_press", "{\"action\":\"timer_cancel\"}");
  returnToNormal();
}

// ============================================================================
// BUTTON HANDLING
// ============================================================================

// Button press callback (called by data_button library)
void onButtonPress() {
  uint32_t now = millis();

  // Transition lockout to prevent rapid state changes from button bounce
  if (transitionInProgress || (now - lastTransitionTime) < TRANSITION_LOCKOUT_MS) {
    DEBUG_PRINTLN("Button press ignored (transition lockout)");
    clearButtonPressed();  // Discard any pending bounces
    return;
  }

  transitionInProgress = true;
  lastTransitionTime = now;

  DEBUG_PRINT("Button pressed in mode: ");
  DEBUG_PRINTLN(currentMode);

  switch (currentMode) {
    case MODE_NORMAL:
      // Start countdown (restartTimer is called inside startCountdown)
      startCountdown();
      break;

    case MODE_COUNTDOWN:
      // Cancel timer, return to normal
      cancelTimer();
      restartTimer("DisplayTick");  // Sync timer phase after cancel
      break;

    case MODE_TIMER:
      // Stop timer, show flashing result
      startFlashingResult();
      restartTimer("DisplayTick");  // Sync timer phase for flash timing
      break;

    case MODE_FLASHING_RESULT:
      // Ignore button presses during flashing
      break;

    case MODE_DISPLAY_RESULT:
      // Start new countdown (restartTimer is called inside startCountdown)
      startCountdown();
      break;
  }
  
  transitionInProgress = false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initDisplayController() {
  DEBUG_PRINTLN("Initializing display controller");
  
  // Set initial state
  showingTime = true;
  currentMode = MODE_NORMAL;
  tickCounter = 0;
  
  // Update both rows atomically (single render during boot)
  updateBothRows();
  
  // Create unified 500ms tick timer (handles all display timing)
  createTimer("DisplayTick", TICK_INTERVAL_MS, unifiedTickCallback);
  
  DEBUG_PRINTLN("Display controller initialized");
  
  // Initialize data libraries LAST - allows display to show immediately during boot
  
  // Time library: NTP sync for Sweden timezone (CET/CEST)
  initTimeData();
  
  // Register time change callback
  setTimeChangeCallback(onTimeChange);
  
  // Temperature library: DS18B20 sensor with automatic 30-second polling
  initTemperatureData();
  
  // Initialize button input (after time/temp so display is already showing)
  initButton();
  
  // Create button polling timer (10ms interval for responsive debouncing)
  createTimer("ButtonPoll", 10, buttonPollCallback);
  
  // Register button press callback
  setButtonPressCallback(onButtonPress);
  
  DEBUG_PRINTLN("Button initialized and callback registered");
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Set display mode
void setDisplayMode(DisplayMode mode) {
  if (currentMode != mode) {
    currentMode = mode;
    tickCounter = 0;  // Reset tick counter on mode change
    DEBUG_PRINT("Display mode set to: ");
    DEBUG_PRINTLN(mode);
  }
}

// Get current display mode
DisplayMode getDisplayMode() {
  return currentMode;
}

// Force update of all display rows
void forceDisplayUpdate() {
  updateBothRows();
}

// Called by data_temperature library when temperature value changes
void updateTemperatureDisplay() {
  // Only update if we're showing temperature (normal mode row 1)
  if (currentMode == MODE_NORMAL) {
    updateBothRows();
  }
  // In timer modes, temperature shows on row 0 when timerTopRowState == 2
  // It will update on the next tick cycle
}

