#include "debug.h"
#include "font_5x7.h"
#include "neopixel_render.h"
#include "wifi_config.h"
#include "led_indicator.h"
#include "ota_update.h"
#include "mqtt_client.h"

void setup() {
  // Initialise serial (only if debug logging enabled)
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("\n\n===========================================");
  DEBUG_PRINTLN("Ski Clock Neo - Starting Up");
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINT("Firmware version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  // Initialize LED indicator and start quick flash immediately
  setupLedIndicator();

  // Initialise WiFi (with configuration portal if needed)
  DEBUG_PRINTLN("Starting WiFi setup...");
  setupWiFi();
  DEBUG_PRINT("WiFi status: ");
  DEBUG_PRINTLN(getWiFiStatus());
  
  // Initialize MQTT system for heartbeat monitoring
  setupMQTT();
  
  // Initialize OTA system (schedules first check in 30s, then hourly)
  setupOTA(30);
  
  // Start NeoPixel updates using platform-appropriate method
  setupNeoPixels();
  
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Setup complete - entering main loop");
  DEBUG_PRINTLN("===========================================\n");

  // Take LED indicator out of setup mode
  enableLedStatusTicker();
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();

  // Handle MQTT updates (subscriptions)
  updateMQTT();
  
  // Handle OTA update retry logic only (checks are triggered by tickers)
  updateOTA();
}
