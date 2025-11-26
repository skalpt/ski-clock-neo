// Includes
#include "ski-clock-neo_config.h"
#include "debug.h"
#include "led_indicator.h"
#include "display_core.h"
#include "timing_helpers.h"
#include "data_button.h"
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

// Debug: track last LED debug print time
static unsigned long lastLedDebug = 0;
static bool ledTestDone = false;

void loop() {
  // DEBUG TEST: After 3 seconds, manually toggle LED from main loop to test GPIO8
  if (!ledTestDone && millis() > 3000) {
    ledTestDone = true;
    DEBUG_PRINTLN("[LED TEST] Testing GPIO8 directly from main loop...");
    
    // Test 1: Direct register manipulation
    DEBUG_PRINTLN("[LED TEST] ledOn() - should turn LED ON");
    ledOn();
    delay(2000);
    
    DEBUG_PRINTLN("[LED TEST] ledOff() - should turn LED OFF");
    ledOff();
    delay(2000);
    
    // Test 2: Try digitalWrite as comparison
    DEBUG_PRINTLN("[LED TEST] digitalWrite(8, LOW) - should turn LED ON (inverted)");
    digitalWrite(LED_PIN, LOW);
    delay(2000);
    
    DEBUG_PRINTLN("[LED TEST] digitalWrite(8, HIGH) - should turn LED OFF (inverted)");
    digitalWrite(LED_PIN, HIGH);
    delay(2000);
    
    // Test 3: Re-run pinMode and try again
    DEBUG_PRINTLN("[LED TEST] Re-running pinMode(8, OUTPUT) then ledOn()");
    pinMode(LED_PIN, OUTPUT);
    ledOn();
    delay(2000);
    ledOff();
    
    DEBUG_PRINTLN("[LED TEST] Test complete. Resuming normal operation.");
  }
  
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();

  // Handle MQTT updates (subscriptions and version requests)
  updateMQTT();
  
  // Update button state (debouncing and callbacks)
  updateButton();
  
  // Update timers (ESP8266 only - loop-driven, non-ISR, WiFi-safe)
  // ESP32 uses FreeRTOS tasks, so no updates needed in loop
  #if defined(ESP8266)
    updateTimers();  // All timer_task managed timers (display, toggle, time check, temperature)
  #endif
  
  // Debug: print LED state every 5 seconds
  if (millis() - lastLedDebug > 5000) {
    lastLedDebug = millis();
    debugLedState();
  }
}
