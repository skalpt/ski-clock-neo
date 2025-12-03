#ifndef NEOPIXEL_RENDER_H
#define NEOPIXEL_RENDER_H

#include "../../ski-clock-neo_config.h" // Import display dimensions from shared config
#include <new>                          // For placement-new operator
#include <Adafruit_NeoPixel.h>          // NeoPixel library
#include "font_5x7.h"                   // Font data
#include "../core/debug.h"              // Debug utilities
#include "display_core.h"               // Core display functions

// ==================== GLOBAL STATE ====================
// Static storage buffer for NeoPixel objects (no heap allocation)
// We'll use placement-new to construct them in initNeoPixels()
extern alignas(Adafruit_NeoPixel) uint8_t rowsStorage[DISPLAY_ROWS * sizeof(Adafruit_NeoPixel)];
extern Adafruit_NeoPixel* rows;

// Internal rendering buffer (sized for max possible configuration)
extern uint8_t neopixelRenderBuffer[MAX_DISPLAY_BUFFER_SIZE];

// ==================== FUNCTION DECLARATIONS ====================
// Setup and rendering functions (pure, no timers)
void initNeoPixels();
void updateNeoPixels();  // Call this externally when display needs refresh
void createNeopixelSnapshot();  // Create snapshot buffer on-demand for MQTT (with proper transforms)

// Utility functions
int charToGlyph(char c);
uint16_t xyToIndex(uint8_t x, uint8_t y);
void indexToXY(uint16_t index, uint8_t &x, uint8_t &y);  // Inverse of xyToIndex
void applySmoothScale2x(const uint8_t* glyphData, uint8_t w0, uint8_t h0, uint8_t out[][20]);

// Drawing primitives (row-aware for variable panel counts)
void setPixelRow(Adafruit_NeoPixel &strip, uint8_t rowIdx, uint8_t x, uint8_t y, uint32_t color);
void drawGlyphForRow(Adafruit_NeoPixel &strip, uint8_t rowIdx, int glyphIndex, int x0, int y0, uint32_t color, uint8_t scale);
uint16_t textWidth(const char *text, uint8_t scale);
void drawTextCenteredForRow(Adafruit_NeoPixel &strip, uint8_t rowIdx, const char *text, uint8_t y0, uint32_t color, uint8_t scale);

#endif
