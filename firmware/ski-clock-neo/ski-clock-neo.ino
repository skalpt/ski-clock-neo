#include <Adafruit_NeoPixel.h>
#include <Ticker.h>
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
Ticker neopixelTicker;
Ticker otaTicker;

// --------------------- Display state ----------------------
int curNum = 0;
volatile bool neopixelUpdateFlag = false;
volatile bool otaCheckFlag = false;
bool initialOtaCheckScheduled = false;
unsigned long initialOtaCheckTime = 0;

// Ticker callbacks - ONLY set flags, no heavy work in ISR!
void neopixelTimerCallback() {
  neopixelUpdateFlag = true;
}

void otaTimerCallback() {
  otaCheckFlag = true;
}

// NeoPixel refresh function (called from loop when flag is set)
void updateNeoPixels() {
  char numStr[2];
  curNum++;
  if (curNum == 10) curNum = 0;
  
  Serial.print("Drawing digit: ");
  Serial.println(curNum);
  
  snprintf(numStr, sizeof(numStr), "%d", curNum);

  // Clear and draw
  row1.clear();
  uint32_t red = row1.Color(255, 0, 0);
  drawTextCentered(row1, numStr, 1, red, 2);
  row1.show();
}

void setup() {
  // Initialise serial (no delay needed - will be ready for first few prints)
  Serial.begin(115200);
  Serial.println("\n\n===========================================");
  Serial.println("Ski Clock Neo - Starting Up");
  Serial.println("===========================================");
  Serial.print("Firmware version: ");
  Serial.println(FIRMWARE_VERSION);

  // Initialize LED indicator FIRST
  setupLED();
  setLedPattern(LED_QUICK_FLASH);  // Show quick flash during setup

  // Initialise WiFi (with configuration portal if needed)
  Serial.println("Starting WiFi setup...");
  setupWiFi();
  Serial.print("WiFi status: ");
  Serial.println(getWiFiStatus());

  // Initialise OTA updates
  setupOTA();

  // Initialise NeoPixels
  row1.begin();
  row1.setBrightness(BRIGHTNESS);
  row1.show();
  Serial.println("NeoPixels initialised.");
  
  // Schedule initial OTA check for 30 seconds from now (non-blocking)
  initialOtaCheckTime = millis() + 30000;
  initialOtaCheckScheduled = true;
  Serial.println("Initial OTA check scheduled for 30 seconds from now");
  
  // Start NeoPixel refresh ticker (2 second interval) - sets flag only
  neopixelTicker.attach_ms(UPDATE_INTERVAL_MS, neopixelTimerCallback);
  Serial.println("NeoPixel ticker started");
  
  // Start OTA check ticker (check every hour) - sets flag only
  otaTicker.attach(3600, otaTimerCallback);  // 3600 seconds = 1 hour
  Serial.println("OTA ticker started (1 hour interval)");
  
  Serial.println("===========================================");
  Serial.println("Setup complete - entering main loop");
  Serial.println("===========================================\n");
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();
  
  // Update LED indicator based on WiFi status
  updateLedStatus();
  
  // Handle OTA update checks (non-blocking, manual checks with retry logic)
  updateOTA();
  
  // Perform scheduled initial OTA check (non-blocking)
  if (initialOtaCheckScheduled && millis() >= initialOtaCheckTime) {
    initialOtaCheckScheduled = false;
    Serial.println("Performing scheduled initial OTA check...");
    forceOTACheck();
  }
  
  // Handle NeoPixel updates when ticker sets flag
  if (neopixelUpdateFlag) {
    neopixelUpdateFlag = false;
    updateNeoPixels();
  }
  
  // Handle OTA checks when ticker sets flag
  if (otaCheckFlag) {
    otaCheckFlag = false;
    Serial.println("Hourly OTA check triggered by ticker");
    forceOTACheck();
  }
  
  // No delay needed - tickers and WiFi portal handle all timing
}
