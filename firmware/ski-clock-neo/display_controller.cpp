#include "display_controller.h"
#include "display.h"
#include "data_time.h"
#include "data_temperature.h"
#include "debug.h"

#if defined(ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#elif defined(ESP8266)
  #include <TickTwo.h>  // Software ticker (loop-driven, non-ISR, WiFi-safe)
#endif

// Controller state
static DisplayMode currentMode = MODE_NORMAL;
static volatile bool showingTime = true;  // Toggle: true = time, false = date (volatile for ESP8266 ISR safety)

// Platform-specific task handles and tickers
#if defined(ESP32)
  static TaskHandle_t controllerTaskHandle = nullptr;  // For 4-second display toggle
#elif defined(ESP8266)
  // Software ticker (loop-driven, non-ISR, safe for setText)
  void toggleTimerCallback();  // Forward declaration
  TickTwo toggleTicker(toggleTimerCallback, 4000, 0, MILLIS);  // 4s, endless, millis resolution (extern in .h)
#endif

// Forward declarations
void updateRow0();
void updateRow1();

// Display controller task (ESP32 only) - handles ONLY the 4-second time/date toggle
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
// ESP8266 timer callback (software ticker, not ISR - safe to call functions directly)
void toggleTimerCallback() {
  // Toggle display and update immediately
  showingTime = !showingTime;
  updateRow0();
}
#endif

void updateRow0() {
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

void initDisplayController(uint8_t temperatureSensorPin) {
  DEBUG_PRINTLN("Initializing display controller");
  
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
    // ESP8266: Software ticker (loop-driven, non-ISR, WiFi-safe)
    toggleTicker.start();
    DEBUG_PRINTLN("Display toggle timer started (ESP8266 TickTwo - loop-driven)");
  #endif
  
  DEBUG_PRINTLN("Display controller initialized");
  
  // Initialize data libraries LAST - allows display to show immediately during boot
  // Time library: NTP sync for Sweden timezone (CET/CEST)
  initTimeData();
  
  // Temperature library: DS18B20 sensor with automatic 30-second polling
  // Temperature library owns ALL temperature timing (tickers, callbacks)
  // Calls updateTemperatureDisplay() callback when value changes
  initTemperatureData(temperatureSensorPin);
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

void updateTemperatureDisplay() {
  // Called by data_temperature library when temperature changes
  updateRow1();
}
