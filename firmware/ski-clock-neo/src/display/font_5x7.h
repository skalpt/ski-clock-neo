#ifndef FONT_5X7_H
#define FONT_5X7_H

#include <Arduino.h> // Include Arduino core for PROGMEM

// -------------------- Glyph indices --------------------
enum GlyphIndex {
  GLYPH_0 = 0,
  GLYPH_1,
  GLYPH_2,
  GLYPH_3,
  GLYPH_4,
  GLYPH_5,
  GLYPH_6,
  GLYPH_7,
  GLYPH_8,
  GLYPH_9,
  GLYPH_MINUS,
  GLYPH_DASH,
  GLYPH_DOT,
  GLYPH_DEGREE,
  GLYPH_C,
  GLYPH_COLON,
  GLYPH_COMMA
};

// -------------------- Font data --------------------
// 5x7 pixel font for digits and special characters
// Each glyph: 7 rows, up to 5 columns (bit 0 = leftmost pixel)
// 1 = pixel on, 0 = off
const uint8_t FONT_5x7[][7] PROGMEM = {
  // '0'
  {
    B01110,
    B10001,
    B10011,
    B10101,
    B11001,
    B10001,
    B01110
  },
  // '1'
  {
    B00100,
    B01100,
    B00100,
    B00100,
    B00100,
    B00100,
    B01110
  },
  // '2'
  {
    B01110,
    B10001,
    B00001,
    B00010,
    B00100,
    B01000,
    B11111
  },
  // '3'
  {
    B01110,
    B10001,
    B00001,
    B00110,
    B00001,
    B10001,
    B01110
  },
  // '4'
  {
    B00010,
    B00110,
    B01010,
    B10010,
    B11111,
    B00010,
    B00010
  },
  // '5'
  {
    B11111,
    B10000,
    B11110,
    B00001,
    B00001,
    B10001,
    B01110
  },
  // '6'
  {
    B00110,
    B01000,
    B10000,
    B11110,
    B10001,
    B10001,
    B01110
  },
  // '7'
  {
    B11111,
    B00001,
    B00010,
    B00100,
    B01000,
    B01000,
    B01000
  },
  // '8'
  {
    B01110,
    B10001,
    B10001,
    B01110,
    B10001,
    B10001,
    B01110
  },
  // '9'
  {
    B01110,
    B10001,
    B10001,
    B01111,
    B00001,
    B00010,
    B01100
  },
  // '-' (minus / short dash)
  {
    B00,
    B00,
    B00,
    B11,
    B00,
    B00,
    B00
  },
  // '—' (long dash)
  {
    B00000,
    B00000,
    B00000,
    B11110,
    B00000,
    B00000,
    B00000
  },
  // '.'
  {
    B0,
    B0,
    B0,
    B0,
    B0,
    B0,
    B1
  },
  // '°'
  {
    B010,
    B101,
    B010,
    B000,
    B000,
    B000,
    B000
  },
  // 'C'
  {
    B01110,
    B10001,
    B10000,
    B10000,
    B10000,
    B10001,
    B01110
  },
  // ':'
  {
    B0,
    B1,
    B1,
    B0,
    B1,
    B1,
    B0
  },
  // ',' (comma - uses drop-case rendering rule, shifted down 1px during render)
  {
    B00,
    B00,
    B00,
    B00,
    B01,
    B01,
    B10
  }
};

// Width of each glyph in pixels
const uint8_t FONT_WIDTH_TABLE[] = {
  5, // 0
  5, // 1
  5, // 2
  5, // 3
  5, // 4
  5, // 5
  5, // 6
  5, // 7
  5, // 8
  5, // 9
  2, // '-' 
  5, // '—' 
  1, // '.'
  3, // '°'
  5, // 'C'
  1, // ':'
  2  // ',' (drop-case glyph)
};

// Font dimensions
const uint8_t FONT_HEIGHT = 7;
const uint8_t CHAR_SPACING = 1;
const bool SPACING_SCALES = false;

#endif
