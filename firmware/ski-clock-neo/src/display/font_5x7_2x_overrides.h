#ifndef FONT_5X7_2X_OVERRIDES_H
#define FONT_5X7_2X_OVERRIDES_H

#include <Arduino.h>
#include "font_5x7.h"

// ============================================================================
// 2x Glyph Override System
// ============================================================================
// Some glyphs don't scale well with automatic 2x smoothing. This file provides
// hand-crafted 2x versions for those specific glyphs.
//
// The automatic smoothing fills in small holes (like in the ° symbol).
// These overrides give full artistic control over the final 2x appearance.
// ============================================================================

// Structure for a 2x override glyph
// Width and height are the SCALED dimensions (2x the original)
struct Glyph2xOverride {
  uint8_t glyphIndex;     // Which glyph this overrides (from GlyphIndex enum)
  uint8_t width;          // Scaled width (typically original * 2)
  uint8_t height;         // Scaled height (typically original * 2, max 14)
  const uint8_t* data;    // Pointer to row data (PROGMEM)
};

// ============================================================================
// Override Glyph Data
// ============================================================================

// Degree symbol (°) at 2x scale - preserves the center hole
// Original 3x3:     2x version 6x6 (with smooth edges AND hole):
//   .#.               ..##..
//   #.#               .####.
//   .#.               ##..##
//                     ##..##
//                     .####.
//                     ..##..
const uint8_t GLYPH_2X_DEGREE_DATA[] PROGMEM = {
  B001100,  // ..##..
  B011110,  // .####.
  B110011,  // ##..##
  B110011,  // ##..##
  B011110,  // .####.
  B001100   // ..##..
};

// ============================================================================
// Override Lookup Table
// ============================================================================

const Glyph2xOverride GLYPH_2X_OVERRIDES[] PROGMEM = {
  { GLYPH_DEGREE, 6, 6, GLYPH_2X_DEGREE_DATA }
};

const uint8_t GLYPH_2X_OVERRIDE_COUNT = sizeof(GLYPH_2X_OVERRIDES) / sizeof(GLYPH_2X_OVERRIDES[0]);

// ============================================================================
// Lookup Function
// ============================================================================

// Find a 2x override for a given glyph index
// Returns pointer to override struct, or nullptr if no override exists
inline const Glyph2xOverride* find2xOverride(uint8_t glyphIndex) {
  for (uint8_t i = 0; i < GLYPH_2X_OVERRIDE_COUNT; i++) {
    uint8_t overrideGlyphIndex = pgm_read_byte(&GLYPH_2X_OVERRIDES[i].glyphIndex);
    if (overrideGlyphIndex == glyphIndex) {
      return &GLYPH_2X_OVERRIDES[i];
    }
  }
  return nullptr;
}

#endif
