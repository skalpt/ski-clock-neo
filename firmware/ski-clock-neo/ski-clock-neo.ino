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

  // Start NeoPixel updates using platform-appropriate method
  setupNeoPixels();
  
  // Initialise WiFi (with configuration portal if needed)
  setupWiFi();
  
  // Initialize MQTT system for heartbeat monitoring
  setupMQTT();
  
  // Initialize OTA system (MQTT-triggered updates)
  setupOTA();
    
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Setup complete - entering main loop");
  DEBUG_PRINTLN("===========================================\n");
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();

  // Handle MQTT updates (subscriptions and version requests)
  updateMQTT();
}
