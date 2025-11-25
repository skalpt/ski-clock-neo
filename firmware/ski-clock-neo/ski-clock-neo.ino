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

#if defined(ESP32)
  #include "esp_system.h"
#endif

const char* getResetReason() {
#if defined(ESP32)
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
    case ESP_RST_POWERON:  return "power_on";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:  return "watchdog_int";
    case ESP_RST_TASK_WDT: return "watchdog_task";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO:     return "sdio";
    default:               return "unknown";
  }
#elif defined(ESP8266)
  rst_info* info = ESP.getResetInfoPtr();
  switch (info->reason) {
    case REASON_DEFAULT_RST:      return "power_on";
    case REASON_WDT_RST:          return "watchdog";
    case REASON_EXCEPTION_RST:    return "crash";
    case REASON_SOFT_WDT_RST:     return "soft_watchdog";
    case REASON_SOFT_RESTART:     return "software";
    case REASON_DEEP_SLEEP_AWAKE: return "deep_sleep";
    case REASON_EXT_SYS_RST:      return "external";
    default:                      return "unknown";
  }
#else
  return "unknown";
#endif
}

void setup() {
  // Initialise serial (only if debug logging enabled)
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("\n\n===========================================");
  DEBUG_PRINTLN("Ski Clock Neo - Starting Up");
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINT("Firmware version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  // Initialize event logging early (before WiFi/MQTT)
  initEventLog();
  
  // Log boot event with reset reason
  String bootData = "{\"reason\":\"";
  bootData += getResetReason();
  bootData += "\",\"version\":\"";
  bootData += FIRMWARE_VERSION;
  bootData += "\"}";
  logEvent("boot", bootData.c_str());

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
