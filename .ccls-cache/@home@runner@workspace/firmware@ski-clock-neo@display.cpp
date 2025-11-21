#include "display.h"

// Display buffer storage (final rendered output for MQTT)
uint8_t displayBuffer[MAX_DISPLAY_BUFFER_SIZE / 8] = {0};
DisplayConfig displayConfig = {0};

// Text content storage (what should be displayed on each row)
char displayText[2][MAX_TEXT_LENGTH] = {{0}, {0}};

void initDisplayBuffer(uint8_t rows, uint8_t panelsPerRow, uint8_t panelWidth, uint8_t panelHeight) {
  displayConfig.rows = rows;
  displayConfig.panelsPerRow = panelsPerRow;
  displayConfig.panelWidth = panelWidth;
  displayConfig.panelHeight = panelHeight;
  
  clearDisplayBuffer();
}

DisplayConfig getDisplayConfig() {
  return displayConfig;
}

void setText(uint8_t row, const char* text) {
  if (row >= 2) return;
  strncpy(displayText[row], text, MAX_TEXT_LENGTH - 1);
  displayText[row][MAX_TEXT_LENGTH - 1] = '\0';
}

const char* getText(uint8_t row) {
  if (row >= 2) return "";
  return displayText[row];
}

void setPixel(uint8_t row, uint16_t x, uint16_t y, bool state) {
  if (row == 0 || row > displayConfig.rows) return;
  if (x >= displayConfig.panelsPerRow * displayConfig.panelWidth) return;
  if (y >= displayConfig.panelHeight) return;
  
  // Calculate pixel index in buffer
  // Each row occupies (panelsPerRow * panelWidth * panelHeight) pixels
  uint16_t rowPixels = displayConfig.panelsPerRow * displayConfig.panelWidth * displayConfig.panelHeight;
  uint16_t pixelIndex = (row - 1) * rowPixels + y * (displayConfig.panelsPerRow * displayConfig.panelWidth) + x;
  
  uint16_t byteIndex = pixelIndex / 8;
  uint8_t bitIndex = pixelIndex % 8;
  
  if (byteIndex >= sizeof(displayBuffer)) return;
  
  if (state) {
    displayBuffer[byteIndex] |= (1 << bitIndex);
  } else {
    displayBuffer[byteIndex] &= ~(1 << bitIndex);
  }
}

void clearDisplayBuffer() {
  memset(displayBuffer, 0, sizeof(displayBuffer));
}

void commitBuffer(const uint8_t* renderBuffer, uint16_t bufferSize) {
  if (bufferSize > sizeof(displayBuffer)) {
    bufferSize = sizeof(displayBuffer);
  }
  memcpy(displayBuffer, renderBuffer, bufferSize);
}

const uint8_t* getDisplayBuffer() {
  return displayBuffer;
}

uint16_t getDisplayBufferSize() {
  // Return actual buffer size needed for current configuration (in bytes)
  uint16_t totalPixels = displayConfig.rows * displayConfig.panelsPerRow * displayConfig.panelWidth * displayConfig.panelHeight;
  return (totalPixels + 7) / 8;
}
