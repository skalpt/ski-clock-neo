#include "debug.h"
#include "font_5x7.h"
#include "neopixel_render.h"
#include "wifi_config.h"
#include "led_indicator.h"
#include "ota_update.h"
#include "mqtt_client.h"
#include "data_time.h"
#include "data_temperature.h"
#include "button.h"
#include "display_controller.h"

// Hardware pin configuration
#define TEMP_SENSOR_PIN 2       // DS18B20 temperature sensor on GPIO2
#define BUTTON_PIN 0            // Button on GPIO0 (boot button on most ESP32/ESP8266 boards)

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

  // Initialize NeoPixel hardware (event-driven, no auto-refresh timers)
  setupNeoPixels();
  
  // Set up render callback: when display content changes, render it
  setRenderCallback(updateNeoPixels);
  DEBUG_PRINTLN("Render callback registered (event-driven updates)");
  
  // Initialise WiFi (with configuration portal if needed)
  setupWiFi();
  
  // Initialize MQTT system for heartbeat monitoring
  setupMQTT();
  
  // Initialize OTA system (MQTT-triggered updates)
  setupOTA();
  
  // Initialize time data (NTP sync with Sweden timezone)
  initTimeData();
  
  // Initialize temperature sensor (DS18B20)
  initTemperatureData(TEMP_SENSOR_PIN);
  
  // Initialize button (debounced, active LOW)
  // Button callbacks can be added later for timer feature
  initButton(BUTTON_PIN, 50);
  
  // Initialize display controller (4-second toggle timer)
  // This will start updating row 0 (time/date) and row 1 (temperature)
  initDisplayController();
  
  // Force initial render to show startup state
  updateNeoPixels();
  
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Setup complete - entering main loop");
  DEBUG_PRINTLN("===========================================\n");
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();

  // Handle MQTT updates (subscriptions and version requests)
  updateMQTT();
  
  // Update display controller (temperature sensor reads, etc.)
  updateDisplayController();
  
  // Update button state (debouncing and callbacks)
  updateButton();
  
  // Drain render queue: Keep processing render requests until none remain
  // This ensures no updates are lost if setText() is called during rendering
  // This is the ONLY place where updateNeoPixels() should be called
  // NOTE: updateNeoPixels() manages clearRenderRequest() internally based on dirty flag
  while (isRenderRequested()) {
    RenderCallback callback = getRenderCallback();
    if (callback != nullptr) {
      callback();  // Safe to call here (main loop context)
      // Callback (updateNeoPixels) clears renderRequested if no more updates pending
    } else {
      // No callback registered, clear request to avoid infinite loop
      clearRenderRequest();
    }
  }
}
