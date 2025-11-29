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

#include "display_controller.h"
#include "display_core.h"
#include "data_time.h"
#include "data_temperature.h"
#include "data_button.h"
#include "event_log.h"
#include "timing_helpers.h"
#include "debug.h"

// ============================================================================
// CONSTANTS
// ============================================================================

static const uint32_t TOGGLE_INTERVAL_MS = 4000;       // Time/date toggle interval
static const uint32_t COUNTDOWN_INTERVAL_MS = 1000;    // Countdown tick (1 second)
static const uint32_t TIMER_INTERVAL_MS = 1000;        // Timer tick (1 second)
static const uint32_t FLASH_INTERVAL_MS = 500;         // Flash interval (0.5 seconds)
static const uint32_t FLASH_DURATION_MS = 8000;        // Flash for 8 seconds
static const uint32_t RESULT_DISPLAY_MS = 60000;       // Show result for 1 minute

// ============================================================================
// STATE VARIABLES
// ============================================================================

static DisplayMode currentMode = MODE_NORMAL;          // Current display mode

// Normal mode toggle state (time vs date)
static volatile bool showingTime = true;

// Timer mode toggle state (cycles through 3 values: 0=time, 1=date, 2=temp)
static volatile uint8_t timerTopRowState = 0;

// Countdown state
static int8_t countdownValue = 3;                      // Countdown: 3, 2, 1

// Timer state
static uint32_t timerStartMillis = 0;                  // When timer started
static uint32_t elapsedSeconds = 0;                    // Elapsed time in seconds

// Flash state
static bool flashVisible = true;                       // Toggle for flashing display
static uint32_t flashStartMillis = 0;                  // When flashing started

// Result display state
static uint32_t resultStartMillis = 0;                 // When result display started

// Transition guard to prevent rapid state changes
static volatile bool transitionInProgress = false;
static uint32_t lastTransitionTime = 0;
static const uint32_t TRANSITION_LOCKOUT_MS = 200;     // Minimum time between transitions

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void toggleTimerCallback();
void timerModeToggleCallback();
void countdownTickCallback();
void timerTickCallback();
void flashTickCallback();
void resultTimeoutCallback();
void buttonPollCallback();
void onTimeChange(uint8_t flags);
void updateRow0();
void updateRow1();
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

// Timer callback for 4-second time/date toggle (normal mode)
void toggleTimerCallback() {
  if (currentMode == MODE_NORMAL) {
    showingTime = !showingTime;
    updateRow0();
  }
}

// Timer callback for 4-second rotation in timer modes (time/date/temp on top row)
void timerModeToggleCallback() {
  if (currentMode == MODE_COUNTDOWN || currentMode == MODE_TIMER) {
    timerTopRowState = (timerTopRowState + 1) % 3;
    updateRow0();
  }
}

// Countdown tick: 3 -> 2 -> 1 -> start timer
void countdownTickCallback() {
  if (currentMode != MODE_COUNTDOWN) return;
  
  countdownValue--;
  
  if (countdownValue <= 0) {
    startTimer();
  } else {
    updateRow1();
  }
}

// Timer tick: increment elapsed time every second
void timerTickCallback() {
  if (currentMode != MODE_TIMER) return;
  
  elapsedSeconds++;
  updateRow1();
}

// Flash tick: toggle visibility every 0.5 seconds
void flashTickCallback() {
  if (currentMode != MODE_FLASHING_RESULT) return;
  
  flashVisible = !flashVisible;
  updateRow1();
  
  // Check if flash duration exceeded
  if (millis() - flashStartMillis >= FLASH_DURATION_MS) {
    startDisplayResult();
  }
}

// Button poll callback: check for button state changes
void buttonPollCallback() {
  updateButton();
}

// Result timeout: auto-return to normal after 1 minute
void resultTimeoutCallback() {
  if (currentMode == MODE_DISPLAY_RESULT) {
    returnToNormal();
  }
}

// Callback for time changes - forces display update when minute or date changes
void onTimeChange(uint8_t flags) {
  DEBUG_PRINT("Time change detected, flags: ");
  DEBUG_PRINTLN(flags);
  
  if (currentMode == MODE_NORMAL) {
    if ((flags & TIME_CHANGE_MINUTE) && showingTime) {
      DEBUG_PRINTLN("Forcing time display update");
      updateRow0();
    }
    if ((flags & TIME_CHANGE_DATE) && !showingTime) {
      DEBUG_PRINTLN("Forcing date display update");
      updateRow0();
    }
  }
}

// ============================================================================
// ROW UPDATE HELPERS
// ============================================================================

// Update row 0 based on current mode
void updateRow0() {
  static char buffer[32];
  
  if (currentMode == MODE_NORMAL) {
    // Normal mode: alternate time/date
    if (!isTimeSynced()) {
      setText(0, "~~.~~");
      DEBUG_PRINTLN("Row 0: Waiting for NTP sync");
      return;
    }
    
    if (showingTime) {
      if (formatTime(buffer, sizeof(buffer))) {
        setText(0, buffer);
        DEBUG_PRINT("Row 0: Time = ");
        DEBUG_PRINTLN(buffer);
      } else {
        setText(0, "~~.~~");
      }
    } else {
      if (formatDate(buffer, sizeof(buffer))) {
        setText(0, buffer);
        DEBUG_PRINT("Row 0: Date = ");
        DEBUG_PRINTLN(buffer);
      }
    }
  } else if (currentMode == MODE_COUNTDOWN || currentMode == MODE_TIMER) {
    // Timer modes: cycle through time/date/temp on top row
    if (!isTimeSynced()) {
      setText(0, "~~.~~");
      return;
    }
    
    switch (timerTopRowState) {
      case 0: // Time
        if (formatTime(buffer, sizeof(buffer))) {
          setText(0, buffer);
        } else {
          setText(0, "~~.~~");
        }
        break;
      case 1: // Date
        if (formatDate(buffer, sizeof(buffer))) {
          setText(0, buffer);
        }
        break;
      case 2: // Temperature
        if (formatTemperature(buffer, sizeof(buffer))) {
          setText(0, buffer);
        } else {
          setText(0, "~~*C");
        }
        break;
    }
  } else if (currentMode == MODE_FLASHING_RESULT || currentMode == MODE_DISPLAY_RESULT) {
    // Result modes: show current time on top row
    if (isTimeSynced() && formatTime(buffer, sizeof(buffer))) {
      setText(0, buffer);
    } else {
      setText(0, "~~.~~");
    }
  }
}

// Update row 1 based on current mode
void updateRow1() {
  static char buffer[32];
  
  if (currentMode == MODE_NORMAL) {
    // Normal mode: show temperature
    if (formatTemperature(buffer, sizeof(buffer))) {
      setText(1, buffer);
      DEBUG_PRINT("Row 1: Temp = ");
      DEBUG_PRINTLN(buffer);
    } else {
      setText(1, "~~*C");
      DEBUG_PRINTLN("Row 1: Temperature not available");
    }
  } else if (currentMode == MODE_COUNTDOWN) {
    // Countdown mode: show countdown number
    snprintf(buffer, sizeof(buffer), "   %d", countdownValue);
    setText(1, buffer);
    DEBUG_PRINT("Row 1: Countdown = ");
    DEBUG_PRINTLN(countdownValue);
  } else if (currentMode == MODE_TIMER) {
    // Timer mode: show elapsed time as MM:SS
    uint32_t mins = elapsedSeconds / 60;
    uint32_t secs = elapsedSeconds % 60;
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu", mins, secs);
    setText(1, buffer);
    DEBUG_PRINT("Row 1: Timer = ");
    DEBUG_PRINTLN(buffer);
  } else if (currentMode == MODE_FLASHING_RESULT) {
    // Flashing result: show elapsed time or blank
    if (flashVisible) {
      uint32_t mins = elapsedSeconds / 60;
      uint32_t secs = elapsedSeconds % 60;
      snprintf(buffer, sizeof(buffer), "%02lu:%02lu", mins, secs);
      setText(1, buffer);
    } else {
      setText(1, "     ");
    }
  } else if (currentMode == MODE_DISPLAY_RESULT) {
    // Display result: show elapsed time solid
    uint32_t mins = elapsedSeconds / 60;
    uint32_t secs = elapsedSeconds % 60;
    snprintf(buffer, sizeof(buffer), "%02lu:%02lu", mins, secs);
    setText(1, buffer);
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
  
  // Stop normal toggle, start timer mode toggle
  stopTimer("DispToggle");
  createTimer("TimerToggle", TOGGLE_INTERVAL_MS, timerModeToggleCallback);
  
  // Start countdown ticker
  createTimer("Countdown", COUNTDOWN_INTERVAL_MS, countdownTickCallback);
  
  logEvent("button_press", "{\"action\":\"timer_start\"}");
  
  updateRow0();
  updateRow1();
}

void startTimer() {
  DEBUG_PRINTLN("Starting timer mode");
  currentMode = MODE_TIMER;
  timerStartMillis = millis();
  elapsedSeconds = 0;
  
  // Stop countdown, start timer tick
  stopTimer("Countdown");
  createTimer("TimerTick", TIMER_INTERVAL_MS, timerTickCallback);
  
  updateRow1();
}

void startFlashingResult() {
  DEBUG_PRINTLN("Starting flashing result mode");
  currentMode = MODE_FLASHING_RESULT;
  flashStartMillis = millis();
  flashVisible = true;
  
  // Stop timer tick and toggle, start flash tick
  stopTimer("TimerTick");
  stopTimer("TimerToggle");
  createTimer("FlashTick", FLASH_INTERVAL_MS, flashTickCallback);
  
  logEvent("button_press", "{\"action\":\"timer_stop\"}");
  
  updateRow0();
  updateRow1();
}

void startDisplayResult() {
  DEBUG_PRINTLN("Starting display result mode");
  currentMode = MODE_DISPLAY_RESULT;
  resultStartMillis = millis();
  
  // Stop flash tick, start result timeout (one-shot, fires after RESULT_DISPLAY_MS)
  stopTimer("FlashTick");
  createOneShotTimer("ResultTimeout", RESULT_DISPLAY_MS, resultTimeoutCallback);
  triggerTimer("ResultTimeout");
  
  updateRow1();
}

void returnToNormal() {
  DEBUG_PRINTLN("Returning to normal mode");
  currentMode = MODE_NORMAL;
  showingTime = true;
  
  // Reset timer state for next use
  timerTopRowState = 0;
  elapsedSeconds = 0;
  countdownValue = 3;
  flashVisible = true;
  
  // Stop any timer mode timers
  stopTimer("TimerToggle");
  stopTimer("Countdown");
  stopTimer("TimerTick");
  stopTimer("FlashTick");
  stopTimer("ResultTimeout");
  
  // Restart normal toggle
  createTimer("DispToggle", TOGGLE_INTERVAL_MS, toggleTimerCallback);
  
  logEvent("display_mode_change", "{\"from\":\"timer\",\"to\":\"normal\"}");
  
  updateRow0();
  updateRow1();
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
    return;
  }
  
  transitionInProgress = true;
  lastTransitionTime = now;
  
  DEBUG_PRINT("Button pressed in mode: ");
  DEBUG_PRINTLN(currentMode);
  
  switch (currentMode) {
    case MODE_NORMAL:
      // Start countdown
      startCountdown();
      break;
      
    case MODE_COUNTDOWN:
      // Cancel timer, return to normal
      cancelTimer();
      break;
      
    case MODE_TIMER:
      // Stop timer, show flashing result
      startFlashingResult();
      break;
      
    case MODE_FLASHING_RESULT:
    case MODE_DISPLAY_RESULT:
      // Ignore button presses during result display
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
  forceDisplayUpdate();
  renderNow();
  
  // Create 4-second toggle timer using timing_helpers library
  createTimer("DispToggle", TOGGLE_INTERVAL_MS, toggleTimerCallback);
  
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
  updateRow0();
  updateRow1();
}

// Called by data_temperature library when temperature value changes
void updateTemperatureDisplay() {
  // Only update row 1 if we're showing temperature there (normal mode)
  if (currentMode == MODE_NORMAL) {
    updateRow1();
  }
  // In timer modes, temperature shows on row 0 when timerTopRowState == 2
  // It will update on the next toggle cycle
}

