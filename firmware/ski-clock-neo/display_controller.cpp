#include "display_controller.h"
#include "display.h"
#include "data_time.h"
#include "data_temperature.h"
#include "debug.h"

#if defined(ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/timers.h>
#elif defined(ESP8266)
  #include <Ticker.h>
#endif

// Controller state
static DisplayMode currentMode = MODE_NORMAL;
static volatile bool showingTime = true;  // Toggle: true = time, false = date (volatile for ISR safety)
static bool initialized = false;

// ESP8266 ISR safety: flag for deferred updates (ISR cannot call heavy functions)
static volatile bool updatePending = false;

// Temperature update tracking
static uint32_t lastTempRequest = 0;  // Will be set on first loop iteration
static const uint32_t TEMP_UPDATE_INTERVAL = 30000;  // 30 seconds
static bool tempRequestPending = true;  // Initial conversion was started in initTemperatureData
static bool firstTempRead = true;

// Timer objects
#if defined(ESP32)
  static TimerHandle_t toggleTimer = nullptr;
#elif defined(ESP8266)
  static Ticker toggleTicker;
#endif

// Forward declarations
void updateRow0();
void updateRow1();

// Timer callback (called every 4 seconds)
// IMPORTANT: Both ESP32 and ESP8266 use deferred updates to avoid heavy work in timer context
#if defined(ESP32)
void toggleTimerCallback(TimerHandle_t xTimer) {
  // ESP32: FreeRTOS timer runs in timer context, avoid mutexes/blocking
  // Just toggle state and set flag - main loop will handle the update
  showingTime = !showingTime;
  updatePending = true;
}
#elif defined(ESP8266)
void IRAM_ATTR toggleTimerCallback() {
  // ESP8266: Ticker runs in ISR context, CANNOT call heavy functions!
  // Just toggle state and set flag - main loop will handle the update
  showingTime = !showingTime;
  updatePending = true;
}
#endif

void updateRow0() {
  if (!initialized) return;
  
  // Check if NTP is synced before attempting to display time/date
  if (!isTimeSynced()) {
    // NTP not synced yet, show placeholder
    setText(0, "SYNC");
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
        setText(0, "--.--");
        DEBUG_PRINTLN("Row 0: Time format failed");
      }
    } else {
      // Show date: "dd-mm"
      if (formatDate(buffer, sizeof(buffer))) {
        setText(0, buffer);
        DEBUG_PRINT("Row 0: Date = ");
        DEBUG_PRINTLN(buffer);
      } else {
        setText(0, "--:--");
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
  if (!initialized) return;
  
  char buffer[32];
  
  // Row 1: Always shows temperature
  if (formatTemperature(buffer, sizeof(buffer))) {
    setText(1, buffer);
    DEBUG_PRINT("Row 1: Temp = ");
    DEBUG_PRINTLN(buffer);
  } else {
    setText(1, "--*C");
    DEBUG_PRINTLN("Row 1: Temperature not available");
  }
}

void initDisplayController() {
  DEBUG_PRINTLN("Initializing display controller");
  
  // Mark as initialized FIRST so update functions can run
  initialized = true;
  
  // Start with time display
  showingTime = true;
  updateRow0();
  updateRow1();
  
  // Create 4-second toggle timer
  #if defined(ESP32)
    toggleTimer = xTimerCreate(
      "DispToggle",                      // Timer name
      pdMS_TO_TICKS(4000),               // Period: 4 seconds
      pdTRUE,                            // Auto-reload
      (void*)0,                          // Timer ID
      toggleTimerCallback                // Callback function
    );
    
    if (toggleTimer != nullptr) {
      xTimerStart(toggleTimer, 0);
      DEBUG_PRINTLN("Display toggle timer started (ESP32 FreeRTOS)");
    } else {
      DEBUG_PRINTLN("ERROR: Failed to create display toggle timer");
    }
  #elif defined(ESP8266)
    toggleTicker.attach(4.0, toggleTimerCallback);  // 4 seconds
    DEBUG_PRINTLN("Display toggle timer started (ESP8266 Ticker)");
  #endif
  
  DEBUG_PRINTLN("Display controller initialized");
}

void setDisplayMode(DisplayMode mode) {
  if (currentMode != mode) {
    currentMode = mode;
    DEBUG_PRINT("Display mode changed to: ");
    DEBUG_PRINTLN(mode == MODE_NORMAL ? "NORMAL" : "TIMER");
    
    // Force immediate update
    forceDisplayUpdate();
  }
}

DisplayMode getDisplayMode() {
  return currentMode;
}

void forceDisplayUpdate() {
  updateRow0();
  updateRow1();
}

void updateDisplayController() {
  if (!initialized) return;
  
  // Handle deferred update from timer callback (both ESP32 and ESP8266)
  if (updatePending) {
    updatePending = false;
    updateRow0();  // Safe to call here (not in timer/ISR context)
  }
  
  uint32_t now = millis();
  
  // Handle initial temperature read (conversion started in initTemperatureData)
  if (firstTempRead && now >= 750) {
    float temp;
    if (getTemperature(&temp)) {
      updateRow1();
      DEBUG_PRINT("Initial temperature: ");
      DEBUG_PRINTLN(temp);
    }
    firstTempRead = false;
    tempRequestPending = false;
    lastTempRequest = now;
    DEBUG_PRINTLN("First temperature read complete");
  }
  
  // Handle temperature sensor updates (non-blocking)
  if (!firstTempRead && !tempRequestPending && (now - lastTempRequest >= TEMP_UPDATE_INTERVAL)) {
    // Request new temperature reading
    requestTemperature();
    tempRequestPending = true;
    lastTempRequest = now;
    DEBUG_PRINTLN("Temperature read requested");
  }
  
  // After ~750ms, read the temperature value
  if (!firstTempRead && tempRequestPending && (now - lastTempRequest >= 750)) {
    float temp;
    if (getTemperature(&temp)) {
      // Temperature updated, refresh row 1
      updateRow1();
      DEBUG_PRINT("Temperature updated: ");
      DEBUG_PRINTLN(temp);
    }
    tempRequestPending = false;
  }
}
