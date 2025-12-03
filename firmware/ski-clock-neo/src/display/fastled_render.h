#ifndef FASTLED_RENDER_H
#define FASTLED_RENDER_H

#include "../../ski-clock-neo_config.h" // Import display dimensions from shared config
#include <FastLED.h>                    // FastLED library for LED control
#include "font_5x7.h"                   // Font data for text rendering
#include "../core/debug.h"              // Debug utilities
#include "display_core.h"               // Core display functionality

extern CRGB* rowLeds[DISPLAY_ROWS];
extern uint16_t rowPixelCounts[DISPLAY_ROWS];

extern uint8_t fastledRenderBuffer[MAX_DISPLAY_BUFFER_SIZE];

void initNeoPixels();
void updateNeoPixels();
void createNeopixelSnapshot();

int charToGlyph(char c);
uint16_t xyToIndex(uint8_t x, uint8_t y);
void indexToXY(uint16_t index, uint8_t &x, uint8_t &y);
void applySmoothScale2x(const uint8_t* glyphData, uint8_t w0, uint8_t h0, uint8_t out[][20]);

uint16_t textWidth(const char *text, uint8_t scale);

#endif
