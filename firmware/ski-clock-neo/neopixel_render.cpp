#include "neopixel_render.h"

// ==================== GLOBAL VARIABLE DEFINITIONS ====================
// Pin array for row initialization (change pins here for all rows)
const uint8_t ROW_PINS[DISPLAY_ROWS] = {4, 3};  // Row 1: GPIO4, Row 2: GPIO3

// Static storage buffer for NeoPixel objects (no heap allocation)
// We'll use placement-new to construct them in setup()
alignas(Adafruit_NeoPixel) uint8_t rowsStorage[DISPLAY_ROWS * sizeof(Adafruit_NeoPixel)];
Adafruit_NeoPixel* rows = reinterpret_cast<Adafruit_NeoPixel*>(rowsStorage);

// Internal rendering buffer (hardware-specific, sized exactly for our config)
uint8_t neopixelRenderBuffer[DISPLAY_BUFFER_SIZE] = {0};

// ESP8266 ticker
#if defined(ESP8266)
  Ticker neopixelTicker;
#endif

// ==================== MAIN RENDERING FUNCTION ====================
void updateNeoPixels() {
  // Clear internal render buffer
  memset(neopixelRenderBuffer, 0, sizeof(neopixelRenderBuffer));
  
  // Loop through each row and render
  for (uint8_t rowIdx = 0; rowIdx < DISPLAY_ROWS; rowIdx++) {
    // Read text from display library (set by external firmware/MQTT/etc)
    const char* displayText = getText(rowIdx);
    
    // Clear this row's NeoPixels
    rows[rowIdx].clear();
    uint32_t red = rows[rowIdx].Color(255, 0, 0);
    
    // Render text using drawTextCentered
    drawTextCentered(rows[rowIdx], displayText, 0, red, 1);
    
    // Copy rendered pixels to internal buffer for this row
    for (uint16_t y = 0; y < ROW_HEIGHT; y++) {
      for (uint16_t x = 0; x < ROW_WIDTH; x++) {
        uint16_t idx = xyToIndex(x, y);
        if (idx < rows[rowIdx].numPixels() && rows[rowIdx].getPixelColor(idx) != 0) {
          // Calculate pixel index in unified buffer (accounting for row offset)
          uint16_t pixelIndex = (rowIdx * ROW_WIDTH * ROW_HEIGHT) + (y * ROW_WIDTH) + x;
          uint16_t byteIndex = pixelIndex / 8;
          uint8_t bitIndex = pixelIndex % 8;
          neopixelRenderBuffer[byteIndex] |= (1 << bitIndex);
        }
      }
    }
    
    // Show on physical NeoPixels for this row
    rows[rowIdx].show();
  }
  
  // Commit complete frame to display library (for MQTT)
  commitBuffer(neopixelRenderBuffer, sizeof(neopixelRenderBuffer));
}

// ==================== FREERTOS TASK (ESP32) ====================
#if defined(ESP32)
void neopixelTask(void* parameter) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(UPDATE_INTERVAL_MS);

  DEBUG_PRINTLN("NeoPixel FreeRTOS task started");

  for(;;) {
    updateNeoPixels();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}
#endif

// ==================== SETUP FUNCTION ====================
void setupNeoPixels() {
  // Initialize display library with actual hardware configuration
  initDisplayBuffer(DISPLAY_ROWS, PANELS_PER_ROW, PANEL_WIDTH, PANEL_HEIGHT);
  
  // Use placement-new to construct NeoPixel objects in loop (dynamic, no heap!)
  for (uint8_t i = 0; i < DISPLAY_ROWS; i++) {
    // Construct Adafruit_NeoPixel at rows[i] using placement-new
    new (&rows[i]) Adafruit_NeoPixel(ROW_WIDTH * ROW_HEIGHT, ROW_PINS[i], NEO_GRB + NEO_KHZ800);
    
    // Initialize the hardware
    rows[i].begin();
    rows[i].setBrightness(BRIGHTNESS);
    rows[i].show();
    
    DEBUG_PRINT("NeoPixel row ");
    DEBUG_PRINT(i + 1);
    DEBUG_PRINT(" (GPIO ");
    DEBUG_PRINT(ROW_PINS[i]);
    DEBUG_PRINTLN(") initialised.");
  }
  
  #if defined(ESP32)
    // ESP32: Use FreeRTOS task for guaranteed timing even during network operations
    // Priority 2 = higher than networking (default priority 1)
    // Stack size: 2KB should be plenty for NeoPixel updates

    #if defined(CONFIG_IDF_TARGET_ESP32C3)
      // ESP32-C3 (single-core RISC-V): Run on Core 0 with high priority
      xTaskCreate(
        neopixelTask,           // Task function
        "NeoPixel",             // Task name
        2048,                   // Stack size (bytes)
        NULL,                   // Task parameter
        2,                      // Priority (2 = higher than default 1)
        NULL                    // Task handle
      );
      DEBUG_PRINTLN("NeoPixel FreeRTOS task started (ESP32-C3: single-core, high priority)");
    #else
      // ESP32/ESP32-S3 (dual-core Xtensa): Pin to Core 1 (APP_CPU)
      // Core 0 handles WiFi/networking, Core 1 handles display
      xTaskCreatePinnedToCore(
        neopixelTask,           // Task function
        "NeoPixel",             // Task name
        2048,                   // Stack size (bytes)
        NULL,                   // Task parameter
        2,                      // Priority
        NULL,                   // Task handle
        1                       // Core 1 (APP_CPU_NUM)
      );
      DEBUG_PRINTLN("NeoPixel FreeRTOS task started (dual-core: pinned to Core 1)");
    #endif

  #elif defined(ESP8266)
    // ESP8266: Use Ticker (no FreeRTOS available)
    neopixelTicker.attach_ms(UPDATE_INTERVAL_MS, updateNeoPixels);
    DEBUG_PRINTLN("NeoPixel ticker started (ESP8266 software ticker)");
  #endif
}

// ==================== UTILITY FUNCTIONS ====================
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

// ==================== DRAWING PRIMITIVES ====================
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
