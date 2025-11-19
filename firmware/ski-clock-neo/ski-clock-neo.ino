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

// -------------------- Pin definitions --------------------
#define PIN_MATRIX_ROW1                  4     // WS2812 data for top row

// -------------------- Display layout ---------------------
const uint8_t ROW_WIDTH                = 16;   // 1 panel x 16 pixels wide
const uint8_t ROW_HEIGHT               = 16;   // All panels 16 pixels high

// ----------- Display brightness & refresh rate -----------
const uint8_t BRIGHTNESS               = 32;   // 0-255 (increased for visibility)
const unsigned long UPDATE_INTERVAL_MS = 2000; // 2 second refresh rate

// ----------------- Declare display rows ------------------
const uint16_t NUM_LEDS_PER_ROW = (uint16_t)ROW_WIDTH * ROW_HEIGHT;
Adafruit_NeoPixel row1(NUM_LEDS_PER_ROW, PIN_MATRIX_ROW1, NEO_GRB + NEO_KHZ800);

// --------------------- Tickers for timing ----------------------
Ticker neopixelTicker;  // Software ticker for NeoPixel updates
// otaTicker is declared in ota_update.h

// --------------------- Display state ----------------------
int curNum = 0;

// NeoPixel refresh function (called directly by software ticker)
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
  
  // Initialize OTA system (schedules first check in 30s, then hourly)
  setupOTA(30);
  
  // Start NeoPixel refresh ticker (2 second interval, software-driven)
  neopixelTicker.attach_ms(UPDATE_INTERVAL_MS, updateNeoPixels);
  DEBUG_PRINTLN("NeoPixel ticker started (2 second interval)");
  
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Setup complete - entering main loop");
  DEBUG_PRINTLN("===========================================\n");
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();
  
  // Update LED pattern based on WiFi status (interrupt ticker handles actual flashing)
  updateLedStatus();
  
  // Handle OTA update retry logic only (checks are triggered by tickers)
  updateOTA();
  
  // NeoPixel updates handled by software ticker (calls updateNeoPixels directly)
  // LED flashing handled by interrupt ticker (calls ledTimerCallback directly)
  // OTA checks scheduled by software ticker (initial: one-shot, recurring: attach after first check)
}
