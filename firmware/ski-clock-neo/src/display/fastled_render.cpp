// ============================================================================
// fastled_render.cpp - FastLED hardware rendering and font drawing
// ============================================================================
// This library handles the FastLED-specific rendering:
// - Manages CRGB arrays for each display row
// - Converts logical (x,y) coordinates to physical strip indices
// - Handles 90° rotation and serpentine wiring patterns
// - Renders 5x7 font glyphs with optional 2x smoothing
// - Creates display snapshots for MQTT publishing
//
// Drop-in replacement for neopixel_render - uses same function names
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "fastled_render.h"        // This file's header
#include "font_5x7_2x_overrides.h" // Hand-crafted 2x glyphs for problem characters

// ============================================================================
// COMPILE-TIME VALIDATION
// ============================================================================

#if DISPLAY_ROWS > 2
  #error "FastLED renderer only supports up to 2 display rows. Add DISPLAY_PIN_ROWx macros and FastLED.addLeds() calls for additional rows."
#endif

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

static CRGB row0Leds[PANEL_WIDTH * PANEL_HEIGHT * 8];
static CRGB row1Leds[PANEL_WIDTH * PANEL_HEIGHT * 8];
CRGB* rowLeds[DISPLAY_ROWS] = {row0Leds, row1Leds};
uint16_t rowPixelCounts[DISPLAY_ROWS];

uint8_t fastledRenderBuffer[MAX_DISPLAY_BUFFER_SIZE] = {0};

static CRGB displayColor;


// ============================================================================
// FORWARD DECLARATIONS (internal functions)
// ============================================================================

static void setPixelRow(uint8_t rowIdx, uint8_t x, uint8_t y, CRGB color);
static void drawGlyphForRow(uint8_t rowIdx, int glyphIndex, int x0, int y0, CRGB color, uint8_t scale);
static void drawTextCenteredForRow(uint8_t rowIdx, const char *text, uint8_t y0, CRGB color, uint8_t scale);

// ============================================================================
// INITIALIZATION
// ============================================================================

void initNeoPixels() {
  DisplayConfig cfg = getDisplayConfig();
  
  displayColor = CRGB(DISPLAY_COLOR_R, DISPLAY_COLOR_G, DISPLAY_COLOR_B);
  
  for (uint8_t i = 0; i < DISPLAY_ROWS; i++) {
    uint16_t rowPixels = cfg.rowConfig[i].width * cfg.rowConfig[i].height;
    rowPixelCounts[i] = rowPixels;
    
    memset(rowLeds[i], 0, sizeof(CRGB) * rowPixels);
    
    DEBUG_PRINT("FastLED row ");
    DEBUG_PRINT(i + 1);
    DEBUG_PRINT(" (");
    DEBUG_PRINT(cfg.rowConfig[i].panels);
    DEBUG_PRINT(" panels, ");
    DEBUG_PRINT(rowPixels);
    DEBUG_PRINT(" pixels, GPIO ");
    DEBUG_PRINT(DISPLAY_PINS[i]);
    DEBUG_PRINTLN(") initialised.");
  }
  
  // Add row 1 first to test if RMT channel order affects first-render bug
  #if DISPLAY_ROWS >= 2
    FastLED.addLeds<NEOPIXEL, DISPLAY_PIN_ROW1>(rowLeds[1], rowPixelCounts[1]);
  #endif
  #if DISPLAY_ROWS >= 1
    FastLED.addLeds<NEOPIXEL, DISPLAY_PIN_ROW0>(rowLeds[0], rowPixelCounts[0]);
  #endif

  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setDither(0);  // Disable temporal dithering to prevent color artifacts at low brightness
  
  FastLED.clear();
  FastLED.show();
  
  DEBUG_PRINTLN("FastLED renderer ready (event-driven, no timers)");
}


// ============================================================================
// MAIN RENDERING
// ============================================================================

void updateNeoPixels() {
  if (!isDisplayDirty()) {
    return;
  }
  
  DEBUG_PRINTLN("[RENDER] updateNeoPixels() called");
  
  uint32_t startSeq = getUpdateSequence();
  
  char textSnapshot[DISPLAY_ROWS][MAX_TEXT_LENGTH];
  snapshotAllText(textSnapshot);
  
  DEBUG_PRINT("[RENDER] Row0 text='");
  DEBUG_PRINT(textSnapshot[0]);
  DEBUG_PRINT("', len=");
  DEBUG_PRINTLN(strlen(textSnapshot[0]));
  DEBUG_PRINT("[RENDER] Row1 text='");
  DEBUG_PRINT(textSnapshot[1]);
  DEBUG_PRINT("', len=");
  DEBUG_PRINTLN(strlen(textSnapshot[1]));
  
  DisplayConfig cfg = getDisplayConfig();
  
  memset(fastledRenderBuffer, 0, cfg.bufferSize);
  
  for (uint8_t rowIdx = 0; rowIdx < DISPLAY_ROWS; rowIdx++) {
    const char* displayText = textSnapshot[rowIdx];
    RowConfig& rowCfg = cfg.rowConfig[rowIdx];
    
    memset(rowLeds[rowIdx], 0, sizeof(CRGB) * rowPixelCounts[rowIdx]);
    
    drawTextCenteredForRow(rowIdx, displayText, 1, displayColor, 2);
    
    for (uint16_t stripIdx = 0; stripIdx < rowPixelCounts[rowIdx]; stripIdx++) {
      if (rowLeds[rowIdx][stripIdx]) {
        uint8_t x, y;
        indexToXY(stripIdx, x, y);
        
        uint16_t pixelIndex = rowCfg.pixelOffset + (y * rowCfg.width) + x;
        uint16_t byteIndex = pixelIndex / 8;
        uint8_t bitIndex = pixelIndex % 8;
        fastledRenderBuffer[byteIndex] |= (1 << bitIndex);
      }
    }
    
    #if ACTIVITY_PIXEL_ENABLED
    // Overlay activity pixel on bottom-right of the last row only
    if (rowIdx == DISPLAY_ROWS - 1) {
      uint8_t activityX = rowCfg.width - 1;
      uint8_t activityY = ROW_HEIGHT - 1;
      uint16_t activityIdx = xyToIndex(activityX, activityY);
      rowLeds[rowIdx][activityIdx] = getActivityPixelVisible() ? displayColor : CRGB::Black;
    }
    #endif
  }
  
  FastLED.show();
  
  bool allDone = clearRenderFlagsIfUnchanged(startSeq);
  (void)allDone;
}

// ============================================================================
// SNAPSHOT CREATION
// ============================================================================

void createNeopixelSnapshot() {
  DisplayConfig cfg = getDisplayConfig();
  
  memset(fastledRenderBuffer, 0, cfg.bufferSize);
  
  for (uint8_t rowIdx = 0; rowIdx < DISPLAY_ROWS; rowIdx++) {
    RowConfig& rowCfg = cfg.rowConfig[rowIdx];
    
    for (uint16_t stripIdx = 0; stripIdx < rowPixelCounts[rowIdx]; stripIdx++) {
      if (rowLeds[rowIdx][stripIdx]) {
        uint8_t x, y;
        indexToXY(stripIdx, x, y);
        
        uint16_t pixelIndex = rowCfg.pixelOffset + (y * rowCfg.width) + x;
        uint16_t byteIndex = pixelIndex / 8;
        uint8_t bitIndex = pixelIndex % 8;
        fastledRenderBuffer[byteIndex] |= (1 << bitIndex);
      }
    }
  }
  
  commitBuffer(fastledRenderBuffer, cfg.bufferSize);
}

// ============================================================================
// COORDINATE TRANSFORMATIONS
// ============================================================================

uint16_t xyToIndex(uint8_t x, uint8_t y) {
  uint8_t panel = x / PANEL_WIDTH;

  uint8_t localX = x % PANEL_WIDTH;
  uint8_t localY = y;

  uint8_t tX = localY;
  uint8_t tY = localX;

  uint16_t base = panel * (PANEL_WIDTH * PANEL_HEIGHT);

  if (tY % 2 == 0) {
    return base + tY * PANEL_HEIGHT + tX;
  } else {
    return base + tY * PANEL_HEIGHT + ((PANEL_HEIGHT - 1) - tX);
  }
}

void indexToXY(uint16_t index, uint8_t &x, uint8_t &y) {
  uint8_t panel = index / (PANEL_WIDTH * PANEL_HEIGHT);
  
  uint16_t localIdx = index % (PANEL_WIDTH * PANEL_HEIGHT);
  
  uint8_t tY = localIdx / PANEL_HEIGHT;
  uint8_t tX;
  
  if (tY % 2 == 0) {
    tX = localIdx % PANEL_HEIGHT;
  } else {
    tX = (PANEL_HEIGHT - 1) - (localIdx % PANEL_HEIGHT);
  }
  
  uint8_t localY = tX;
  uint8_t localX = tY;
  
  x = panel * PANEL_WIDTH + localX;
  y = localY;
}

// ============================================================================
// FONT RENDERING HELPERS
// ============================================================================

int charToGlyph(char c) {
  if (c >= '0' && c <= '9') {
    return (int)(c - '0');
  }
  if (c == '-') {
    return GLYPH_MINUS;
  }
  if (c == '~') {
    return GLYPH_DASH;
  }
  if (c == '.') {
    return GLYPH_DOT;
  }
  if (c == '*' || (uint8_t)c == 0xB0) {
    return GLYPH_DEGREE;
  }
  if (c == 'C' || c == 'c') {
    return GLYPH_C;
  }
  if (c == ':') {
    return GLYPH_COLON;
  }
  return -1;
}

void applySmoothScale2x(const uint8_t* glyphData,
                        uint8_t w0,
                        uint8_t h0,
                        uint8_t out[][20])
{
  const uint8_t W = w0 * 2;
  const uint8_t H = h0 * 2;

  memset(out, 0, H * 20);
  
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

  for (uint8_t r = 0; r + 1 < h0; r++) {
    uint8_t bits0 = pgm_read_byte(&glyphData[r]);
    uint8_t bits1 = pgm_read_byte(&glyphData[r+1]);

    for (uint8_t c = 0; c + 1 < w0; c++) {
      uint8_t a = (bits0 & (1 << (w0 - 1 - c)))     ? 1 : 0;
      uint8_t b = (bits0 & (1 << (w0 - 1 - (c+1)))) ? 1 : 0;
      uint8_t d = (bits1 & (1 << (w0 - 1 - c)))     ? 1 : 0;
      uint8_t e = (bits1 & (1 << (w0 - 1 - (c+1)))) ? 1 : 0;

      uint8_t R = r * 2;
      uint8_t C = c * 2;

      if (a==0 && b==1 && d==1 && e==0) {
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

      if (a==1 && b==0 && d==0 && e==1) {
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

// ============================================================================
// DRAWING PRIMITIVES
// ============================================================================

static void setPixelRow(uint8_t rowIdx, uint8_t x, uint8_t y, CRGB color) {
  DisplayConfig cfg = getDisplayConfig();
  if (rowIdx >= cfg.rows) return;
  if (x >= cfg.rowConfig[rowIdx].width || y >= ROW_HEIGHT) return;
  uint16_t idx = xyToIndex(x, y);
  if (idx < rowPixelCounts[rowIdx]) {
    rowLeds[rowIdx][idx] = color;
  }
}

static void drawGlyphForRow(uint8_t rowIdx,
               int glyphIndex,
               int x0, int y0,
               CRGB color,
               uint8_t scale)
{
  if (glyphIndex < 0) return;

  const uint8_t w0 = FONT_WIDTH_TABLE[glyphIndex];
  const uint8_t h0 = FONT_HEIGHT;

  if (scale == 2) {
    const Glyph2xOverride* override = find2xOverride(glyphIndex);
    
    if (override != nullptr) {
      uint8_t overrideW = pgm_read_byte(&override->width);
      uint8_t overrideH = pgm_read_byte(&override->height);
      const uint8_t* overrideData = (const uint8_t*)pgm_read_ptr(&override->data);
      
      for (uint8_t r = 0; r < overrideH; r++) {
        uint8_t bits = pgm_read_byte(&overrideData[r]);
        for (uint8_t c = 0; c < overrideW; c++) {
          bool on = bits & (1 << (overrideW - 1 - c));
          if (on) {
            setPixelRow(rowIdx, x0 + c, y0 + r, color);
          }
        }
      }
      return;
    }
    
    const uint8_t W = w0 * 2;
    const uint8_t H = h0 * 2;

    static uint8_t glyphBuffer[14][20];

    applySmoothScale2x(&FONT_5x7[glyphIndex][0], w0, h0, glyphBuffer);

    for (uint8_t r = 0; r < H; r++)
      for (uint8_t c = 0; c < W; c++)
        if (glyphBuffer[r][c])
          setPixelRow(rowIdx, x0 + c, y0 + r, color);

    return;
  }

  for (uint8_t r = 0; r < h0; r++) {
    uint8_t bits = pgm_read_byte(&FONT_5x7[glyphIndex][r]);
    for (uint8_t c = 0; c < w0; c++) {
      bool on = bits & (1 << (w0 - 1 - c));
      if (!on) continue;

      for (uint8_t dy=0; dy<scale; dy++)
      for (uint8_t dx=0; dx<scale; dx++)
        setPixelRow(rowIdx, x0 + c*scale + dx,
                          y0 + r*scale + dy,
                          color);
    }
  }
}

// ============================================================================
// TEXT RENDERING
// ============================================================================

uint16_t textWidth(const char *text, uint8_t scale) {
  uint16_t width = 0;

  for (uint8_t i = 0; text[i] != '\0'; i++) {
    int gi = charToGlyph(text[i]);
    if (gi >= 0) {
      if (scale == 2) {
        const Glyph2xOverride* override = find2xOverride(gi);
        if (override != nullptr) {
          width += pgm_read_byte(&override->width);
        } else {
          width += FONT_WIDTH_TABLE[gi] * scale;
        }
      } else {
        width += FONT_WIDTH_TABLE[gi] * scale;
      }

      if (text[i+1] != '\0') {
        width += SPACING_SCALES ? (CHAR_SPACING * scale)
                                : CHAR_SPACING;
      }
    }
  }

  return width;
}

static void drawTextCenteredForRow(uint8_t rowIdx,
                      const char *text,
                      uint8_t y0,
                      CRGB color,
                      uint8_t scale)
{
  DisplayConfig cfg = getDisplayConfig();
  uint16_t rowWidth = cfg.rowConfig[rowIdx].width;
  
  uint16_t w = textWidth(text, scale);
  int16_t x0 = (rowWidth - w) / 2;

  for (uint8_t i = 0; text[i] != '\0'; i++) {
    int gi = charToGlyph(text[i]);
    if (gi >= 0) {
      drawGlyphForRow(rowIdx, gi, x0, y0, color, scale);

      uint8_t charW;
      if (scale == 2) {
        const Glyph2xOverride* override = find2xOverride(gi);
        if (override != nullptr) {
          charW = pgm_read_byte(&override->width);
        } else {
          charW = FONT_WIDTH_TABLE[gi] * scale;
        }
      } else {
        charW = FONT_WIDTH_TABLE[gi] * scale;
      }
      
      uint8_t spacing = SPACING_SCALES ? (CHAR_SPACING * scale)
                                       : CHAR_SPACING;

      x0 += charW + spacing;
    }
  }
}
