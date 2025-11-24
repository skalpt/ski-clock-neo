#include "display_controller.h"
#include "display.h"
#include "data_time.h"
#include "data_temperature.h"
#include "debug.h"

#include <Ticker.h>  // Used for temperature timing on both ESP32 and ESP8266

#if defined(ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#endif

// Controller state
static DisplayMode currentMode = MODE_NORMAL;
static volatile bool showingTime = true;  // Toggle: true = time, false = date (volatile for ESP8266 ISR safety)
static bool initialized = false;

// Temperature update tracking (using Tickers for cleaner timing)
static bool temperatureRequestPending = true;  // Initial conversion was started in initTemperatureData
static bool firstTemperatureRead = true;

// FreeRTOS task handle and Tickers
#if defined(ESP32)
  static TaskHandle_t controllerTaskHandle = nullptr;
  static Ticker temperaturePollTicker;    // 30-second temperature polling ticker
  static Ticker temperatureReadTicker;    // 750ms temperature read ticker
#elif defined(ESP8266)
  // ESP8266: Fallback using Ticker with flag-polling pattern for display toggle
  static Ticker toggleTicker;
  static Ticker temperaturePollTicker;    // 30-second temperature polling ticker
  static Ticker temperatureReadTicker;    // 750ms temperature read ticker
  static volatile bool updatePending = false;  // For display toggle only
#endif

// Forward declarations
void updateRow0();
void updateRow1();

// Temperature ticker callbacks (software tickers, not ISR context)
void temperaturePollCallback() {
  if (!temperatureRequestPending) {
    // Request new temperature reading
    requestTemperature();
    temperatureRequestPending = true;
    DEBUG_PRINTLN("Temperature read requested (ticker)");
    
    // Schedule read after 750ms
    temperatureReadTicker.once(0.75, temperatureReadCallback);
  }
}

void temperatureReadCallback() {
  float temp;
  if (getTemperature(&temp)) {
    updateRow1();
    DEBUG_PRINT("Temperature updated: ");
    DEBUG_PRINTLN(temp);
    
    // First read complete
    if (firstTemperatureRead) {
      firstTemperatureRead = false;
      DEBUG_PRINTLN("First temperature read complete");
    }
  }
  temperatureRequestPending = false;
}

// Display controller task (ESP32 only) - handles ONLY the 4-second time/date toggle
// Temperature timing is handled by Tickers (no contention with display cadence)
#if defined(ESP32)
void displayControllerTask(void* parameter) {
  DEBUG_PRINTLN("Display controller FreeRTOS task started");
  DEBUG_PRINTLN("Task focus: 4-second time/date toggle only");
  DEBUG_PRINTLN("Temperature timing handled by Tickers (no contention)");
  
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t toggleInterval = pdMS_TO_TICKS(4000);  // 4 seconds for time/date toggle
  
  for(;;) {
    // Sleep until next 4-second interval (precise timing with vTaskDelayUntil)
    // This MUST come first to ensure proper cadence (first toggle after 4s, not immediately)
    vTaskDelayUntil(&lastWakeTime, toggleInterval);
    
    // Update time/date display (toggle every 4 seconds)
    // This is the ONLY job of this task - deterministic display cadence
    showingTime = !showingTime;
    updateRow0();
  }
}
#elif defined(ESP8266)
// ESP8266 timer callback (ISR context, flag-based deferral required)
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
  
  // Create FreeRTOS task for display toggle (ESP32) or Ticker (ESP8266)
  #if defined(ESP32)
    // ESP32: Use dedicated FreeRTOS task for deterministic 4-second time/date toggle
    // Priority 2 (same as renderer) ensures display cadence isn't blocked by network I/O
    // Stack size: 2KB (minimal - only handles time/date toggle)
    
    #if defined(CONFIG_IDF_TARGET_ESP32C3)
      // ESP32-C3 (single-core RISC-V): Run on Core 0 with priority 2
      xTaskCreate(
        displayControllerTask,      // Task function
        "DispCtrl",                 // Task name
        2048,                       // Stack size (bytes) - reduced, no temp logic
        NULL,                       // Task parameter
        2,                          // Priority (2 = same as renderer)
        &controllerTaskHandle       // Task handle
      );
      DEBUG_PRINTLN("Display controller FreeRTOS task created (ESP32-C3: 4s toggle only)");
    #else
      // ESP32/ESP32-S3 (dual-core Xtensa): Pin to Core 1 (APP_CPU, same as renderer)
      xTaskCreatePinnedToCore(
        displayControllerTask,      // Task function
        "DispCtrl",                 // Task name
        2048,                       // Stack size (bytes) - reduced, no temp logic
        NULL,                       // Task parameter
        2,                          // Priority (2 = same as renderer)
        &controllerTaskHandle,      // Task handle
        1                           // Core 1 (APP_CPU_NUM)
      );
      DEBUG_PRINTLN("Display controller FreeRTOS task created (dual-core: 4s toggle only)");
    #endif
    
  #elif defined(ESP8266)
    // ESP8266: Fallback to Ticker with flag-polling (no true FreeRTOS)
    toggleTicker.attach(4.0, toggleTimerCallback);  // 4 seconds
    DEBUG_PRINTLN("Display toggle timer started (ESP8266 Ticker)");
  #endif
  
  // Start temperature tickers (both ESP32 and ESP8266)
  // These handle ALL temperature timing - no contention with display task
  temperaturePollTicker.attach(30.0, temperaturePollCallback);  // Poll every 30 seconds
  DEBUG_PRINTLN("Temperature poll ticker started (30 seconds)");
  
  // Initial temperature read (conversion already started in initTemperatureData)
  temperatureReadTicker.once(0.75, temperatureReadCallback);  // Read after 750ms
  DEBUG_PRINTLN("Initial temperature read scheduled (750ms)");
  
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
  
  // ESP8266 only: Handle deferred display update from timer callback
  // Temperature updates are handled directly in ticker callbacks (no flags needed)
  #if defined(ESP8266)
    if (updatePending) {
      updatePending = false;
      updateRow0();  // Safe to call here (not in ISR context)
    }
  #endif
  
  // ESP32: Nothing to do - FreeRTOS task handles display, tickers handle temperature
  // This function is effectively a no-op on ESP32
}
