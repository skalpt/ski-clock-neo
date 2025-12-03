// Includes
#include "ski-clock-neo_config.h"
#include "src/core/debug.h"
#include "src/core/timer_helpers.h"
#include "src/core/event_log.h"
#include "src/core/led_indicator.h"
#include "src/core/device_config.h"
#include "src/display/display_core.h"
#include "src/connectivity/wifi_config.h"
#include "src/connectivity/mqtt_client.h"

void setup() {
  // Initialise serial (only if debug logging enabled)
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("\n\n===========================================");
  DEBUG_PRINTLN("Ski Clock Neo - Starting Up");
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINT("Firmware version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  // Initialize event logging and log boot event
  initEventLog();
  logBootEvent();

  // Initialize device configuration (loads from NVS/EEPROM)
  initDeviceConfig();

  // Initialize onboard LED indicator
  initLedIndicator();

  // Initialize display (includes time, temperature, and button initialization)
  initDisplay();

  // Initialise WiFi
  initWiFi();
  
  // Initialize MQTT system
  initMQTT();
    
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Setup complete - entering main loop");
  DEBUG_PRINTLN("===========================================\n");
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();

  // Handle MQTT updates (subscriptions and version requests)
  updateMQTT();
  
  // Update timers (ESP8266 only - loop-driven, non-ISR, WiFi-safe)
  // ESP32 uses FreeRTOS tasks, so no updates needed in loop
  #if defined(ESP8266)
    updateTimers();  // All timer_task managed timers (display, toggle, time check, temperature, button)
  #endif
}
