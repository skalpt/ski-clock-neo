#include <Adafruit_NeoPixel.h>
#include "font_5x7.h"
#include "neopixel_render.h"
#include "wifi_config.h"

// -------------------- Firmware version -------------------
#define FIRMWARE_VERSION "v1.0.0"

// GitHub repo configuration comes from Replit secrets (injected at build time)
// No need to hardcode these values here anymore

#include "ota_update.h"

// -------------------- Pin definitions --------------------
#define PIN_MATRIX_ROW1                  4     // WS2812 data for top row

// -------------------- Display layout ---------------------
const uint8_t ROW_WIDTH                = 16;   // 1 panel x 16 pixels wide
const uint8_t ROW_HEIGHT               = 16;   // All panels 16 pixels high

// ----------- Display brightness & refresh rate -----------
const uint8_t BRIGHTNESS               = 1;    // 0-255
const unsigned long UPDATE_INTERVAL_MS = 2000; // 2 second refresh rate

// ----------------- Declare display rows ------------------
const uint16_t NUM_LEDS_PER_ROW = (uint16_t)ROW_WIDTH * ROW_HEIGHT;
Adafruit_NeoPixel row1(NUM_LEDS_PER_ROW, PIN_MATRIX_ROW1, NEO_GRB + NEO_KHZ800);

// --------------------- Setup & loop ----------------------
unsigned long lastUpdateMs = 0;
int curNum = 0;

void setup() {
  // Initialise serial
  Serial.begin(115200);
  Serial.println("Booted.");
  Serial.print("Firmware version: ");
  Serial.println(FIRMWARE_VERSION);

  // Initialise WiFi (with configuration portal if needed)
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
  
  // Perform initial OTA check after 30 seconds
  delay(30000);
  forceOTACheck();
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();
  
  // Handle OTA update checks (non-blocking, checks once per hour by default)
  updateOTA();

  unsigned long nowMs = millis();
  if (nowMs - lastUpdateMs < UPDATE_INTERVAL_MS) {
    return;
  }
  lastUpdateMs = nowMs;

  char numStr[2]; // "%d\0"
  curNum++;
  if (curNum == 10) curNum = 0;
  Serial.print("Drawing digit: ");
  Serial.println (curNum);
  
  snprintf(numStr, sizeof(numStr), "%d", curNum);

  // ----- Clear rows -----
  row1.clear();

  uint32_t red = row1.Color(255, 0, 0);

  // Draw time on row 1
  // vertically centered: place at y= (16-14)/2 = 1
  drawTextCentered(row1, numStr, 1, red, 2);

  row1.show();
}
