#ifndef SCALE_SMOOTH_H
#define SCALE_SMOOTH_H

#include <Arduino.h>

// Apply diagonal smoothing to a 2x scaled bitmap
// Takes the original glyph data and applies anti-aliasing patterns
// where diagonal edges occur
//
// Parameters:
//   glyphData: pointer to the original 7-row glyph in PROGMEM
//   w0: original width of the glyph (columns)
//   h0: original height of the glyph (rows, typically 7)
//   out: output buffer [H][W] where H=h0*2, W=w0*2
//
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

#endif
