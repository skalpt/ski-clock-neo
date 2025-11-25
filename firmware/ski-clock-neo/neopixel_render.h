#ifndef NEOPIXEL_RENDER_H
#define NEOPIXEL_RENDER_H

#include "ski-clock-neo_config.h" // Import display dimensions from shared config
#include <new>              // For placement-new operator
#include <Adafruit_NeoPixel.h>
#include "font_5x7.h"
#include "debug.h"
#include "display.h"

// ==================== GLOBAL STATE ====================
// Static storage buffer for NeoPixel objects (no heap allocation)
// We'll use placement-new to construct them in initNeoPixels()
extern alignas(Adafruit_NeoPixel) uint8_t rowsStorage[DISPLAY_ROWS * sizeof(Adafruit_NeoPixel)];
extern Adafruit_NeoPixel* rows;

// Internal rendering buffer (hardware-specific, sized exactly for our config)
extern uint8_t neopixelRenderBuffer[DISPLAY_BUFFER_SIZE];

// ==================== FUNCTION DECLARATIONS ====================
// Setup and rendering functions (pure, no timers)
void initNeoPixels();
void updateNeoPixels();  // Call this externally when display needs refresh
void createSnapshotBuffer();  // Create snapshot buffer on-demand for MQTT (with proper transforms)

// Utility functions
int charToGlyph(char c);
uint16_t xyToIndex(uint8_t x, uint8_t y);
void indexToXY(uint16_t index, uint8_t &x, uint8_t &y);  // Inverse of xyToIndex
void applySmoothScale2x(const uint8_t* glyphData, uint8_t w0, uint8_t h0, uint8_t out[][20]);

// Drawing primitives
void setPixelRow(Adafruit_NeoPixel &strip, uint8_t x, uint8_t y, uint32_t color);
void drawGlyph(Adafruit_NeoPixel &strip, int glyphIndex, int x0, int y0, uint32_t color, uint8_t scale);
uint16_t textWidth(const char *text, uint8_t scale);
void drawTextCentered(Adafruit_NeoPixel &strip, const char *text, uint8_t y0, uint32_t color, uint8_t scale);

#endif
