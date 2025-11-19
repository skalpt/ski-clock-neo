#include <Adafruit_NeoPixel.h>
#include "font_5x7.h"
#include "scale_smooth.h"

// -------------------- Pin definitions --------------------
#define PIN_MATRIX_ROW1  4   // WS2812 data for top row

// -------------------- Display layout ----------------------
const uint8_t ROW_WIDTH   = 16;   // 1 x 16
const uint8_t ROW_HEIGHT  = 16;
const uint16_t NUM_LEDS_PER_ROW = (uint16_t)ROW_WIDTH * ROW_HEIGHT;

const uint8_t BRIGHTNESS  = 1;   // 0-255, adjust for your power budget
const unsigned long UPDATE_INTERVAL_MS = 2000;

// -------------------- NeoPixel objects --------------------
Adafruit_NeoPixel row1(NUM_LEDS_PER_ROW, PIN_MATRIX_ROW1, NEO_GRB + NEO_KHZ800);

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
  // NEW — pass scale!
  uint16_t w = textWidth(text, scale);
  int16_t x0 = (ROW_WIDTH - w) / 2;

  for (uint8_t i = 0; text[i] != '\0'; i++) {
    int gi = charToGlyph(text[i]);
    if (gi >= 0) {
      drawGlyph(strip, gi, x0, y0, color, scale);

      uint8_t charW = FONT_WIDTH_TABLE[gi] * scale;

      // NEW — spacing depends on mode
      uint8_t spacing = SPACING_SCALES ? (CHAR_SPACING * scale)
                                       : CHAR_SPACING;

      x0 += charW + spacing;
    }
  }
}

// -------------------- Setup & loop -------------------------
unsigned long lastUpdateMs = 0;
int curNum = 0;

void setup() {
  // Serial optional
  Serial.begin(115200);
  Serial.println("Booted.");

  // NeoPixels
  row1.begin();
  row1.setBrightness(BRIGHTNESS);
  row1.show();
  Serial.println("NeoPixels initialised.");
}

void loop() {
  unsigned long nowMs = millis();
  if (nowMs - lastUpdateMs < UPDATE_INTERVAL_MS) {
    return;
  }
  lastUpdateMs = nowMs;

  char numStr[2]; // "%d\0"
  curNum++;
  if (curNum == 10) curNum = 0;
  Serial.print("Drawing digit: ");
  Serial.println (curNum);
  
  snprintf(numStr, sizeof(numStr), "%d", curNum);

  // ----- Clear rows -----
  row1.clear();

  uint32_t red = row1.Color(255, 0, 0);

  // Draw time on row 1
  // vertically centered: place at y= (16-14)/2 = 1
  drawTextCentered(row1, numStr, 1, red, 2);

  row1.show();
}
