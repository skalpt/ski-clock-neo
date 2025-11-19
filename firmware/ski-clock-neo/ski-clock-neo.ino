#include <Adafruit_NeoPixel.h>
#include <Ticker.h>
#include "debug.h"
#include "font_5x7.h"
#include "neopixel_render.h"
#include "wifi_config.h"
#include "led_indicator.h"

// -------------------- Firmware version -------------------
// Version is injected at build time via compiler flags
// Fallback to default if not injected (for local development)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.0.0"
#endif

// GitHub repo configuration comes from Replit secrets (injected at build time)
// No need to hardcode these values here anymore

#include "ota_update.h"
#include "mqtt_client.h"

// -------------------- Pin definitions --------------------
#define PIN_MATRIX_ROW1                  4     // WS2812 data for top row

// -------------------- Display layout ---------------------
const uint8_t ROW_WIDTH                = 16;   // 1 panel x 16 pixels wide
const uint8_t ROW_HEIGHT               = 16;   // All panels 16 pixels high

// ----------- Display brightness & refresh rate -----------
const uint8_t BRIGHTNESS               = 1;    // 0-255 (keeping it as dim as possible while I'm developing in the office)
const unsigned long UPDATE_INTERVAL_MS = 200; // 0.2 second refresh rate

// ----------------- Declare display rows ------------------
const uint16_t NUM_LEDS_PER_ROW = (uint16_t)ROW_WIDTH * ROW_HEIGHT;
Adafruit_NeoPixel row1(NUM_LEDS_PER_ROW, PIN_MATRIX_ROW1, NEO_GRB + NEO_KHZ800);

// --------------------- Tickers for timing ----------------------
#if defined(ESP8266)
  Ticker neopixelTicker;  // Software ticker for NeoPixel updates (ESP8266 only)
#endif
// otaTicker is declared in ota_update.h

// --------------------- Display state ----------------------
int curNum = 0;

// NeoPixel refresh function (called by ticker or FreeRTOS task)
void updateNeoPixels() {
  char numStr[2];
  curNum++;
  if (curNum == 10) curNum = 0;
  
  DEBUG_PRINT("Drawing digit: ");
  DEBUG_PRINTLN(curNum);
  
  snprintf(numStr, sizeof(numStr), "%d", curNum);

  // Clear and draw
  row1.clear();
  uint32_t red = row1.Color(255, 0, 0);
  drawTextCentered(row1, numStr, 1, red, 2);
  row1.show();
}

#if defined(ESP32)
// FreeRTOS task for NeoPixel updates (ESP32 only - preempts network operations)
void neopixelTask(void* parameter) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(UPDATE_INTERVAL_MS);
  
  DEBUG_PRINTLN("NeoPixel FreeRTOS task started");
  
  for(;;) {
    updateNeoPixels();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}
#endif

void setup() {
  // Initialise serial (only if debug logging enabled)
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("\n\n===========================================");
  DEBUG_PRINTLN("Ski Clock Neo - Starting Up");
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINT("Firmware version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  // Initialize LED indicator and start quick flash immediately
  setupLED();
  setLedPattern(LED_QUICK_FLASH);  // Interrupt-driven ticker starts now

  // Initialise WiFi (with configuration portal if needed)
  DEBUG_PRINTLN("Starting WiFi setup...");
  setupWiFi();
  DEBUG_PRINT("WiFi status: ");
  DEBUG_PRINTLN(getWiFiStatus());

  // Initialise NeoPixels
  row1.begin();
  row1.setBrightness(BRIGHTNESS);
  row1.show();
  DEBUG_PRINTLN("NeoPixels initialised.");
  
  // Initialize MQTT system for heartbeat monitoring
  setupMQTT();
  
  // Initialize OTA system (schedules first check in 30s, then hourly)
  setupOTA(30);
  
  // Start NeoPixel updates using platform-appropriate method
#if defined(ESP32)
  // ESP32: Use FreeRTOS task for guaranteed timing even during network operations
  // Priority 2 = higher than networking (default priority 1)
  // Stack size: 2KB should be plenty for NeoPixel updates
  
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3 (single-core RISC-V): Run on Core 0 with high priority
    xTaskCreate(
      neopixelTask,           // Task function
      "NeoPixel",             // Task name
      2048,                   // Stack size (bytes)
      NULL,                   // Task parameter
      2,                      // Priority (2 = higher than default 1)
      NULL                    // Task handle
    );
    DEBUG_PRINTLN("NeoPixel FreeRTOS task started (ESP32-C3: single-core, high priority)");
  #else
    // ESP32/ESP32-S3 (dual-core Xtensa): Pin to Core 1 (APP_CPU)
    // Core 0 handles WiFi/networking, Core 1 handles display
    xTaskCreatePinnedToCore(
      neopixelTask,           // Task function
      "NeoPixel",             // Task name
      2048,                   // Stack size (bytes)
      NULL,                   // Task parameter
      2,                      // Priority
      NULL,                   // Task handle
      1                       // Core 1 (APP_CPU_NUM)
    );
    DEBUG_PRINTLN("NeoPixel FreeRTOS task started (dual-core: pinned to Core 1)");
  #endif
  
#elif defined(ESP8266)
  // ESP8266: Use Ticker (no FreeRTOS available)
  neopixelTicker.attach_ms(UPDATE_INTERVAL_MS, updateNeoPixels);
  DEBUG_PRINTLN("NeoPixel ticker started (ESP8266 software ticker)");
#endif
  
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Setup complete - entering main loop");
  DEBUG_PRINTLN("===========================================\n");
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();
  
  // Update LED pattern based on WiFi status (interrupt ticker handles actual flashing)
  updateLedStatus();
  
  // Handle MQTT heartbeat and version update subscriptions
  loopMQTT(FIRMWARE_VERSION);
  
  // Handle OTA update retry logic only (checks are triggered by tickers)
  updateOTA();
  
  // Platform-specific display updates:
  // - ESP32: FreeRTOS task (high priority or Core 1) - preempts network operations
  // - ESP8266: Software ticker (shares CPU with network)
  // LED flashing: Hardware interrupt timer (all platforms) - guaranteed execution
  // OTA checks: Software ticker (initial: one-shot, recurring: attach after first check)
  // MQTT heartbeat: 60-second publish cycle with non-blocking reconnection
}
