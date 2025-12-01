// ============================================================================
// neopixel_render.cpp - NeoPixel hardware rendering and font drawing
// ============================================================================
// This library handles the NeoPixel-specific rendering:
// - Manages Adafruit_NeoPixel instances for each display row
// - Converts logical (x,y) coordinates to physical strip indices
// - Handles 90° rotation and serpentine wiring patterns
// - Renders 5x7 font glyphs with optional 2x smoothing
// - Creates display snapshots for MQTT publishing
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "neopixel_render.h"  // This file's header

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// Static storage buffer for NeoPixel objects (no heap allocation)
alignas(Adafruit_NeoPixel) uint8_t rowsStorage[DISPLAY_ROWS * sizeof(Adafruit_NeoPixel)];
Adafruit_NeoPixel* rows = reinterpret_cast<Adafruit_NeoPixel*>(rowsStorage);

// Internal rendering buffer (sized for max possible configuration)
uint8_t neopixelRenderBuffer[MAX_DISPLAY_BUFFER_SIZE] = {0};

// ============================================================================
// INITIALIZATION
// ============================================================================

void initNeoPixels() {
  // Use placement-new to construct NeoPixel objects without heap allocation
  // Each row may have a different number of panels (and thus different pixel count)
  DisplayConfig cfg = getDisplayConfig();
  
  for (uint8_t i = 0; i < DISPLAY_ROWS; i++) {
    // Calculate pixel count for this row from its configuration
    uint16_t rowPixels = cfg.rowConfig[i].width * cfg.rowConfig[i].height;
    
    // Construct Adafruit_NeoPixel at rows[i] with row-specific pixel count
    new (&rows[i]) Adafruit_NeoPixel(rowPixels, DISPLAY_PINS[i], NEO_GRB + NEO_KHZ800);
    
    // Initialize the hardware
    rows[i].begin();
    rows[i].setBrightness(BRIGHTNESS);
    rows[i].clear();  // Zero the strip buffer before first show
    rows[i].show();
    
    DEBUG_PRINT("NeoPixel row ");
    DEBUG_PRINT(i + 1);
    DEBUG_PRINT(" (");
    DEBUG_PRINT(cfg.rowConfig[i].panels);
    DEBUG_PRINT(" panels, ");
    DEBUG_PRINT(rowPixels);
    DEBUG_PRINT(" pixels, GPIO ");
    DEBUG_PRINT(DISPLAY_PINS[i]);
    DEBUG_PRINTLN(") initialised.");
  }
  
  DEBUG_PRINTLN("NeoPixel renderer ready (event-driven, no timers)");
}

// ============================================================================
// MAIN RENDERING
// ============================================================================

// Update NeoPixels from display text (called when display is dirty)
void updateNeoPixels() {
  // Skip rendering if display hasn't changed
  if (!isDisplayDirty()) {
    return;
  }
  
  // Capture current update sequence BEFORE rendering
  uint32_t startSeq = getUpdateSequence();
  
  // Atomically snapshot all text to prevent torn reads
  char textSnapshot[DISPLAY_ROWS][MAX_TEXT_LENGTH];
  snapshotAllText(textSnapshot);
  
  // Get display configuration for per-row dimensions
  DisplayConfig cfg = getDisplayConfig();
  
  // Clear internal render buffer
  memset(neopixelRenderBuffer, 0, cfg.bufferSize);
  
  // Loop through each row and render from snapshot
  for (uint8_t rowIdx = 0; rowIdx < DISPLAY_ROWS; rowIdx++) {
    const char* displayText = textSnapshot[rowIdx];
    RowConfig& rowCfg = cfg.rowConfig[rowIdx];
    
    // Clear this row's NeoPixels
    rows[rowIdx].clear();
    uint32_t color = rows[rowIdx].Color(DISPLAY_COLOR_R, DISPLAY_COLOR_G, DISPLAY_COLOR_B);
    
    // Render text using drawTextCentered (with row-specific width)
    drawTextCenteredForRow(rows[rowIdx], rowIdx, displayText, 1, color, 2);
    
    // Copy rendered pixels to internal buffer for MQTT snapshots
    for (uint16_t stripIdx = 0; stripIdx < rows[rowIdx].numPixels(); stripIdx++) {
      if (rows[rowIdx].getPixelColor(stripIdx) != 0) {
        // Reverse transformation: strip index → logical (x, y)
        uint8_t x, y;
        indexToXY(stripIdx, x, y);
        
        // Calculate pixel index in unified buffer using per-row offset
        uint16_t pixelIndex = rowCfg.pixelOffset + (y * rowCfg.width) + x;
        uint16_t byteIndex = pixelIndex / 8;
        uint8_t bitIndex = pixelIndex % 8;
        neopixelRenderBuffer[byteIndex] |= (1 << bitIndex);
      }
    }
    
    // Show on physical NeoPixels
    rows[rowIdx].show();
  }
  
  // Atomically clear flags if sequence unchanged
  bool allDone = clearRenderFlagsIfUnchanged(startSeq);
  (void)allDone;  // Suppress unused warning
}

// ============================================================================
// SNAPSHOT CREATION
// ============================================================================

// Create snapshot buffer on-demand (called when publishing MQTT snapshot)
void createNeopixelSnapshot() {
  // Get display configuration for per-row dimensions
  DisplayConfig cfg = getDisplayConfig();
  
  // Clear buffer
  memset(neopixelRenderBuffer, 0, cfg.bufferSize);
  
  // Loop through each row and copy current NeoPixel state
  for (uint8_t rowIdx = 0; rowIdx < DISPLAY_ROWS; rowIdx++) {
    RowConfig& rowCfg = cfg.rowConfig[rowIdx];
    
    for (uint16_t stripIdx = 0; stripIdx < rows[rowIdx].numPixels(); stripIdx++) {
      if (rows[rowIdx].getPixelColor(stripIdx) != 0) {
        // Reverse transformation: strip index → logical (x, y)
        uint8_t x, y;
        indexToXY(stripIdx, x, y);
        
        // Calculate pixel index in unified buffer using per-row offset
        uint16_t pixelIndex = rowCfg.pixelOffset + (y * rowCfg.width) + x;
        uint16_t byteIndex = pixelIndex / 8;
        uint8_t bitIndex = pixelIndex % 8;
        neopixelRenderBuffer[byteIndex] |= (1 << bitIndex);
      }
    }
  }
  
  // Commit to display library for MQTT publishing
  commitBuffer(neopixelRenderBuffer, cfg.bufferSize);
}

// ============================================================================
// COORDINATE TRANSFORMATIONS
// ============================================================================

// Convert logical (x,y) to NeoPixel strip index
// Handles 90° rotation and serpentine wiring within each panel
uint16_t xyToIndex(uint8_t x, uint8_t y) {
  // Determine which panel we are in
  uint8_t panel = x / PANEL_WIDTH;

  // Local x,y within the panel
  uint8_t localX = x % PANEL_WIDTH;
  uint8_t localY = y;

  // Transform: rotate 90° clockwise
  uint8_t tX = localY;     // final X (range: 0 to PANEL_HEIGHT-1)
  uint8_t tY = localX;     // final Y (range: 0 to PANEL_WIDTH-1)

  // Panel offset (each panel has PANEL_WIDTH × PANEL_HEIGHT pixels)
  uint16_t base = panel * (PANEL_WIDTH * PANEL_HEIGHT);

  // Apply serpentine wiring within each panel
  if (tY % 2 == 0) {
    // even row: left→right
    return base + tY * PANEL_HEIGHT + tX;
  } else {
    // odd row: right→left
    return base + tY * PANEL_HEIGHT + ((PANEL_HEIGHT - 1) - tX);
  }
}

// Inverse: convert NeoPixel strip index to logical (x, y)
void indexToXY(uint16_t index, uint8_t &x, uint8_t &y) {
  // Determine which panel
  uint8_t panel = index / (PANEL_WIDTH * PANEL_HEIGHT);
  
  // Get local index within the panel
  uint16_t localIdx = index % (PANEL_WIDTH * PANEL_HEIGHT);
  
  // Reverse serpentine
  uint8_t tY = localIdx / PANEL_HEIGHT;
  uint8_t tX;
  
  if (tY % 2 == 0) {
    tX = localIdx % PANEL_HEIGHT;
  } else {
    tX = (PANEL_HEIGHT - 1) - (localIdx % PANEL_HEIGHT);
  }
  
  // Reverse 90° clockwise rotation
  uint8_t localY = tX;
  uint8_t localX = tY;
  
  // Convert back to global coordinates
  x = panel * PANEL_WIDTH + localX;
  y = localY;
}

// ============================================================================
// FONT RENDERING HELPERS
// ============================================================================

// Map character to glyph index
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
  // Unsupported character
  return -1;
}

// Apply 2x scaling with diagonal smoothing
void applySmoothScale2x(const uint8_t* glyphData,
                        uint8_t w0,
                        uint8_t h0,
                        uint8_t out[][20])
{
  const uint8_t W = w0 * 2;
  const uint8_t H = h0 * 2;

  // Step 1: Build the normal 2× scaled bitmap
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

  // Step 2: Check 2×2 cells for diagonal patterns and smooth them
  for (uint8_t r = 0; r + 1 < h0; r++) {
    uint8_t bits0 = pgm_read_byte(&glyphData[r]);
    uint8_t bits1 = pgm_read_byte(&glyphData[r+1]);

    for (uint8_t c = 0; c + 1 < w0; c++) {
      // Extract 2x2 pattern
      uint8_t a = (bits0 & (1 << (w0 - 1 - c)))     ? 1 : 0;
      uint8_t b = (bits0 & (1 << (w0 - 1 - (c+1)))) ? 1 : 0;
      uint8_t d = (bits1 & (1 << (w0 - 1 - c)))     ? 1 : 0;
      uint8_t e = (bits1 & (1 << (w0 - 1 - (c+1)))) ? 1 : 0;

      uint8_t R = r * 2;
      uint8_t C = c * 2;

      // Pattern A: Top-right to bottom-left diagonal
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

      // Pattern B: Top-left to bottom-right diagonal
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

// Set a single pixel on a NeoPixel row (with row-specific width check)
void setPixelRow(Adafruit_NeoPixel &strip, uint8_t rowIdx, uint8_t x, uint8_t y, uint32_t color) {
  DisplayConfig cfg = getDisplayConfig();
  if (rowIdx >= cfg.rows) return;
  if (x >= cfg.rowConfig[rowIdx].width || y >= ROW_HEIGHT) return;
  uint16_t idx = xyToIndex(x, y);
  if (idx < strip.numPixels()) {
    strip.setPixelColor(idx, color);
  }
}

// Draw a single glyph at specified position (row-aware)
void drawGlyphForRow(Adafruit_NeoPixel &strip,
               uint8_t rowIdx,
               int glyphIndex,
               int x0, int y0,
               uint32_t color,
               uint8_t scale)
{
  if (glyphIndex < 0) return;

  const uint8_t w0 = FONT_WIDTH_TABLE[glyphIndex];
  const uint8_t h0 = FONT_HEIGHT;

  // Special handling for scale == 2 with diagonal smoothing
  if (scale == 2) {
    const uint8_t W = w0 * 2;
    const uint8_t H = h0 * 2;

    // Static buffer to reduce stack pressure
    static uint8_t glyphBuffer[14][20];

    // Apply 2x scaling with diagonal smoothing
    applySmoothScale2x(&FONT_5x7[glyphIndex][0], w0, h0, glyphBuffer);

    // Draw the smoothed result
    for (uint8_t r = 0; r < H; r++)
      for (uint8_t c = 0; c < W; c++)
        if (glyphBuffer[r][c])
          setPixelRow(strip, rowIdx, x0 + c, y0 + r, color);

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
        setPixelRow(strip, rowIdx, x0 + c*scale + dx,
                          y0 + r*scale + dy,
                          color);
    }
  }
}

// ============================================================================
// TEXT RENDERING
// ============================================================================

// Calculate width of text string in pixels
uint16_t textWidth(const char *text, uint8_t scale) {
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

// Draw text centered on a specific row (uses row-specific width for centering)
void drawTextCenteredForRow(Adafruit_NeoPixel &strip,
                      uint8_t rowIdx,
                      const char *text,
                      uint8_t y0,
                      uint32_t color,
                      uint8_t scale)
{
  DisplayConfig cfg = getDisplayConfig();
  uint16_t rowWidth = cfg.rowConfig[rowIdx].width;
  
  uint16_t w = textWidth(text, scale);
  int16_t x0 = (rowWidth - w) / 2;

  for (uint8_t i = 0; text[i] != '\0'; i++) {
    int gi = charToGlyph(text[i]);
    if (gi >= 0) {
      drawGlyphForRow(strip, rowIdx, gi, x0, y0, color, scale);

      uint8_t charW = FONT_WIDTH_TABLE[gi] * scale;
      uint8_t spacing = SPACING_SCALES ? (CHAR_SPACING * scale)
                                       : CHAR_SPACING;

      x0 += charW + spacing;
    }
  }
}
