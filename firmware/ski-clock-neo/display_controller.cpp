#include "display_controller.h"
#include "display.h"
#include "data_time.h"
#include "data_temperature.h"
#include "debug.h"

#if defined(ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#elif defined(ESP8266)
  #include <Ticker.h>
#endif

// Controller state
static DisplayMode currentMode = MODE_NORMAL;
static volatile bool showingTime = true;  // Toggle: true = time, false = date (volatile for ESP8266 ISR safety)
static bool initialized = false;

// Temperature update tracking
static uint32_t lastTempRequest = 0;
static const uint32_t TEMP_UPDATE_INTERVAL = 30000;  // 30 seconds
static bool tempRequestPending = true;  // Initial conversion was started in initTemperatureData
static bool firstTempRead = true;

// FreeRTOS task handle (ESP32 only)
#if defined(ESP32)
  static TaskHandle_t controllerTaskHandle = nullptr;
#elif defined(ESP8266)
  // ESP8266: Fallback using Ticker with flag-polling pattern
  static Ticker toggleTicker;
  static volatile bool updatePending = false;
#endif

// Forward declarations
void updateRow0();
void updateRow1();

// Display controller task - handles content scheduling independently of main loop
// This ensures display updates are deterministic and not blocked by network operations
#if defined(ESP32)
void displayControllerTask(void* parameter) {
  DEBUG_PRINTLN("Display controller FreeRTOS task started");
  
  // Wait for initial temperature conversion (started in initTemperatureData)
  vTaskDelay(pdMS_TO_TICKS(750));
  float temp;
  if (getTemperature(&temp)) {
    updateRow1();
    DEBUG_PRINT("Initial temperature: ");
    DEBUG_PRINTLN(temp);
  }
  firstTempRead = false;
  tempRequestPending = false;
  lastTempRequest = millis();
  
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t toggleInterval = pdMS_TO_TICKS(4000);  // 4 seconds for time/date toggle
  
  for(;;) {
    // Sleep until next 4-second interval (precise timing with vTaskDelayUntil)
    // This MUST come first to ensure proper cadence (first toggle after 4s, not immediately)
    vTaskDelayUntil(&lastWakeTime, toggleInterval);
    
    // Update time/date display (toggle every 4 seconds)
    showingTime = !showingTime;
    updateRow0();
    
    // Check if it's time to update temperature (every 30 seconds)
    uint32_t now = millis();
    if (!tempRequestPending && (now - lastTempRequest >= TEMP_UPDATE_INTERVAL)) {
      requestTemperature();
      tempRequestPending = true;
      lastTempRequest = now;
      DEBUG_PRINTLN("Temperature read requested");
      
      // Wait 750ms for temperature conversion, then read
      vTaskDelay(pdMS_TO_TICKS(750));
      float temp;
      if (getTemperature(&temp)) {
        updateRow1();
        DEBUG_PRINT("Temperature updated: ");
        DEBUG_PRINTLN(temp);
      }
      tempRequestPending = false;
    }
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
  
  // Create FreeRTOS task for content scheduling (ESP32) or Ticker (ESP8266)
  #if defined(ESP32)
    // ESP32: Use dedicated FreeRTOS task for deterministic timing
    // Priority 2 (same as renderer) ensures content scheduling isn't blocked by network I/O
    // Stack size: 3KB for time/date/temperature formatting
    
    #if defined(CONFIG_IDF_TARGET_ESP32C3)
      // ESP32-C3 (single-core RISC-V): Run on Core 0 with priority 2
      xTaskCreate(
        displayControllerTask,      // Task function
        "DispCtrl",                 // Task name
        3072,                       // Stack size (bytes)
        NULL,                       // Task parameter
        2,                          // Priority (2 = same as renderer)
        &controllerTaskHandle       // Task handle
      );
      DEBUG_PRINTLN("Display controller FreeRTOS task created (ESP32-C3: single-core, priority 2)");
    #else
      // ESP32/ESP32-S3 (dual-core Xtensa): Pin to Core 1 (APP_CPU, same as renderer)
      xTaskCreatePinnedToCore(
        displayControllerTask,      // Task function
        "DispCtrl",                 // Task name
        3072,                       // Stack size (bytes)
        NULL,                       // Task parameter
        2,                          // Priority (2 = same as renderer)
        &controllerTaskHandle,      // Task handle
        1                           // Core 1 (APP_CPU_NUM)
      );
      DEBUG_PRINTLN("Display controller FreeRTOS task created (dual-core: pinned to Core 1, priority 2)");
    #endif
    
  #elif defined(ESP8266)
    // ESP8266: Fallback to Ticker with flag-polling (no true FreeRTOS)
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
  #if defined(ESP32)
    // ESP32: Controller runs in dedicated FreeRTOS task, main loop does nothing
    // All content scheduling (time/date/temperature) handled independently
    return;
  #elif defined(ESP8266)
    // ESP8266: Fallback to flag-based polling (no true FreeRTOS task support)
    if (!initialized) return;
    
    // Handle deferred update from timer callback
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
  #endif
}
