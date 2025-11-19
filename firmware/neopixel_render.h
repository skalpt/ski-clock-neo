#ifndef NEOPIXEL_RENDER_H
#define NEOPIXEL_RENDER_H

#include <Adafruit_NeoPixel.h>
#include "font_5x7.h"

// Forward declarations for display dimensions (defined in main sketch)
extern const uint8_t ROW_WIDTH;
extern const uint8_t ROW_HEIGHT;

// -------------------- Utility: char -> glyph index ----------
int charToGlyph(char c) {
  if (c >= '0' && c <= '9') {
    return (int)(c - '0'); // 0-9
  }
  if (c == '-') {
    return GLYPH_MINUS;
  }
  if (c == '.') {
    return GLYPH_DOT;
  }
  if (c == '*' || (uint8_t)c == 0xB0) {
    // Support both '*' placeholder and actual degree symbol (extended ASCII 0xB0)
    return GLYPH_DEGREE;
  }
  if (c == 'C' || c == 'c') {
    return GLYPH_C;
  }
  if (c == ':') {
    return GLYPH_COLON;
  }
  // Unsupported character -> treat as space
  return -1;
}

// -------------------- Utility: XY to NeoPixel index --------
uint16_t xyToIndex(uint8_t x, uint8_t y) {
  // Determine which panel we are in (0,1,2)
  uint8_t panel = x / 16;

  // Local x,y within the 16x16 matrix
  uint8_t localX = x % 16;
  uint8_t localY = y;

  // ---- Transform: rotate 90° clockwise, then mirror horizontally ----
  // Combined effect = swap x <-> y
  uint8_t tX = localY;     // final X
  uint8_t tY = localX;     // final Y

  // Panel offset
  uint16_t base = panel * 256;

  // Apply standard serpentine wiring within each 16×16 panel
  if (tY % 2 == 0) {
    // even row: left→right
    return base + tY * 16 + tX;
  } else {
    // odd row: right→left
    return base + tY * 16 + (15 - tX);
  }
}

// -------------------- 2x Scaling with Diagonal Smoothing ----
// Apply diagonal smoothing to a 2x scaled bitmap
// Takes the original glyph data and applies anti-aliasing patterns
// where diagonal edges occur
void applySmoothScale2x(const uint8_t* glyphData,
                        uint8_t w0,
                        uint8_t h0,
                        uint8_t out[][20])  // Max width: 10 columns * 2 = 20
{
  const uint8_t W = w0 * 2;
  const uint8_t H = h0 * 2;

  // Step 1: Build the normal 2× scaled bitmap
  memset(out, 0, H * 20);  // Clear entire buffer
  
  for (uint8_t r = 0; r < h0; r++) {
    uint8_t bits = pgm_read_byte(&glyphData[r]);
    for (uint8_t c = 0; c < w0; c++) {
      bool on = bits & (1 << (w0 - 1 - c));
      if (!on) continue;

      out[r*2    ][c*2    ] = 1;
      out[r*2    ][c*2 + 1] = 1;
      out[r*2 + 1][c*2    ] = 1;
      out[r*2 + 1][c*2 + 1] = 1;
    }
  }

  // Step 2: Check original 2×2 cells for diagonal patterns and smooth them
  for (uint8_t r = 0; r + 1 < h0; r++) {
    uint8_t bits0 = pgm_read_byte(&glyphData[r]);
    uint8_t bits1 = pgm_read_byte(&glyphData[r+1]);

    for (uint8_t c = 0; c + 1 < w0; c++) {
      // Extract 2x2 pattern from original glyph:
      //   a b
      //   d e
      uint8_t a = (bits0 & (1 << (w0 - 1 - c)))     ? 1 : 0;  // row r, col c
      uint8_t b = (bits0 & (1 << (w0 - 1 - (c+1)))) ? 1 : 0;  // row r, col c+1
      uint8_t d = (bits1 & (1 << (w0 - 1 - c)))     ? 1 : 0;  // row r+1, col c
      uint8_t e = (bits1 & (1 << (w0 - 1 - (c+1)))) ? 1 : 0;  // row r+1, col c+1

      uint8_t R = r * 2;  // Top-left of 4x4 block in output
      uint8_t C = c * 2;

      // Pattern A: Top-right to bottom-left diagonal
      //   0 1
      //   1 0
      if (a==0 && b==1 && d==1 && e==0)
      {
        const uint8_t patch[4][4] = {
          {0,0,1,1},
          {0,1,1,1},
          {1,1,1,0},
          {1,1,0,0}
        };

        for (uint8_t rr=0; rr<4; rr++)
          for (uint8_t cc=0; cc<4; cc++)
            if (patch[rr][cc]) out[R+rr][C+cc] = 1;
      }

      // Pattern B: Top-left to bottom-right diagonal
      //   1 0
      //   0 1
      if (a==1 && b==0 && d==0 && e==1)
      {
        const uint8_t patch[4][4] = {
          {1,1,0,0},
          {1,1,1,0},
          {0,1,1,1},
          {0,0,1,1}
        };

        for (uint8_t rr=0; rr<4; rr++)
          for (uint8_t cc=0; cc<4; cc++)
            if (patch[rr][cc]) out[R+rr][C+cc] = 1;
      }
    }
  }
}

// -------------------- Drawing primitives -------------------
void setPixelRow(Adafruit_NeoPixel &strip, uint8_t x, uint8_t y, uint32_t color) {
  if (x >= ROW_WIDTH || y >= ROW_HEIGHT) return;
  uint16_t idx = xyToIndex(x, y);
  if (idx < strip.numPixels()) {
    strip.setPixelColor(idx, color);
  }
}

void drawGlyph(Adafruit_NeoPixel &strip,
               int glyphIndex,
               int x0, int y0,
               uint32_t color,
               uint8_t scale)
{
  if (glyphIndex < 0) return;

  const uint8_t w0 = FONT_WIDTH_TABLE[glyphIndex];
  const uint8_t h0 = FONT_HEIGHT;

  // Special handling for scale == 2 with diagonal smoothing
  if (scale == 2)
  {
    const uint8_t W = w0 * 2;
    const uint8_t H = h0 * 2;

    // Use static buffer to reduce stack pressure (reused across calls)
    static uint8_t glyphBuffer[14][20];  // Max: 7*2 rows, 5*2*2 cols

    // Apply 2x scaling with diagonal smoothing
    applySmoothScale2x(&FONT_5x7[glyphIndex][0], w0, h0, glyphBuffer);

    // Draw the smoothed result to the display
    for (uint8_t r = 0; r < H; r++)
      for (uint8_t c = 0; c < W; c++)
        if (glyphBuffer[r][c])
          setPixelRow(strip, x0 + c, y0 + r, color);

    return;
  }

  // Normal scaling for scale != 2
  for (uint8_t r = 0; r < h0; r++) {
    uint8_t bits = pgm_read_byte(&FONT_5x7[glyphIndex][r]);
    for (uint8_t c = 0; c < w0; c++) {
      bool on = bits & (1 << (w0 - 1 - c));
      if (!on) continue;

      for (uint8_t dy=0; dy<scale; dy++)
      for (uint8_t dx=0; dx<scale; dx++)
        setPixelRow(strip, x0 + c*scale + dx,
                          y0 + r*scale + dy,
                          color);
    }
  }
}

uint16_t textWidth(const char *text, uint8_t scale)
{
  uint16_t width = 0;

  for (uint8_t i = 0; text[i] != '\0'; i++) {
    int gi = charToGlyph(text[i]);
    if (gi >= 0) {
      width += FONT_WIDTH_TABLE[gi] * scale;

      if (text[i+1] != '\0') {
        width += SPACING_SCALES ? (CHAR_SPACING * scale)
                                : CHAR_SPACING;
      }
    }
  }

  return width;
}

void drawTextCentered(Adafruit_NeoPixel &strip,
                      const char *text,
                      uint8_t y0,
                      uint32_t color,
                      uint8_t scale)
{
  uint16_t w = textWidth(text, scale);
  int16_t x0 = (ROW_WIDTH - w) / 2;

  for (uint8_t i = 0; text[i] != '\0'; i++) {
    int gi = charToGlyph(text[i]);
    if (gi >= 0) {
      drawGlyph(strip, gi, x0, y0, color, scale);

      uint8_t charW = FONT_WIDTH_TABLE[gi] * scale;
      uint8_t spacing = SPACING_SCALES ? (CHAR_SPACING * scale)
                                       : CHAR_SPACING;

      x0 += charW + spacing;
    }
  }
}

#endif
