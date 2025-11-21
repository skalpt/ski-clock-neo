#ifndef NEOPIXEL_RENDER_H
#define NEOPIXEL_RENDER_H

#include "display_config.h" // Import display dimensions from shared config
#include <new>              // For placement-new operator
#include <Adafruit_NeoPixel.h>
#include "font_5x7.h"
#include <Ticker.h>
#include "debug.h"
#include "display.h"

// ==================== CONSTANTS ====================
// Display settings
#define BRIGHTNESS 1                    // 0-255 (keeping dim for development)
#define UPDATE_INTERVAL_MS 200          // 0.2 second refresh rate

extern const uint8_t ROW_PINS[DISPLAY_ROWS];  // Pin array for row initialization

// ==================== GLOBAL STATE ====================
// Static storage buffer for NeoPixel objects (no heap allocation)
// We'll use placement-new to construct them in setup()
extern alignas(Adafruit_NeoPixel) uint8_t rowsStorage[DISPLAY_ROWS * sizeof(Adafruit_NeoPixel)];
extern Adafruit_NeoPixel* rows;

// Internal rendering buffer (hardware-specific, sized exactly for our config)
extern uint8_t neopixelRenderBuffer[DISPLAY_BUFFER_SIZE];

// ==================== PLATFORM-SPECIFIC DECLARATIONS ====================
#if defined(ESP8266)
  extern Ticker neopixelTicker;
#endif

// ==================== FUNCTION DECLARATIONS ====================
// Setup and refresh functions
void setupNeoPixels();
void updateNeoPixels();

// FreeRTOS task (ESP32 only)
#if defined(ESP32)
  void neopixelTask(void* parameter);
#endif

// Utility functions
int charToGlyph(char c);
uint16_t xyToIndex(uint8_t x, uint8_t y);
void applySmoothScale2x(const uint8_t* glyphData, uint8_t w0, uint8_t h0, uint8_t out[][20]);

// Drawing primitives
void setPixelRow(Adafruit_NeoPixel &strip, uint8_t x, uint8_t y, uint32_t color);
void drawGlyph(Adafruit_NeoPixel &strip, int glyphIndex, int x0, int y0, uint32_t color, uint8_t scale);
uint16_t textWidth(const char *text, uint8_t scale);
void drawTextCentered(Adafruit_NeoPixel &strip, const char *text, uint8_t y0, uint32_t color, uint8_t scale);

#endif
