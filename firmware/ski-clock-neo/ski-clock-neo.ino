// Includes
#include "ski-clock-neo_config.h"
#include "debug.h"
#include "led_indicator.h"
#include "display.h"
#include "display_controller.h"
#include "data_temperature.h"
#include "button.h"
#include "wifi_config.h"
#include "mqtt_client.h"
#include "event_log.h"

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

  // Initialize onboard LED indicator
  initLedIndicator();

  // Initialize display
  initDisplay();
  
  // Initialize button
  initButton();

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
  
  // Update button state (debouncing and callbacks)
  updateButton();
  
  // Update TickTwo software tickers (ESP8266 only - loop-driven, non-ISR, WiFi-safe)
  // ESP32 uses FreeRTOS tasks, so no ticker updates needed
  #if defined(ESP8266)
    displayTicker.update();             // Display rendering (1ms poll, safe for NeoPixel)
    toggleTicker.update();              // Display time/date toggle (4s)
    temperaturePollTicker.update();     // Temperature poll (30s)
    temperatureReadTicker.update();     // Temperature read delay (750ms)
  #endif
}
