#include "display.h"
#include <string.h>

// Display buffer storage (final rendered output for MQTT)
// Size is determined by DISPLAY_BUFFER_SIZE from the hardware renderer
uint8_t displayBuffer[DISPLAY_BUFFER_SIZE] = {0};
DisplayConfig displayConfig = {0};

// Text content storage (what should be displayed on each row)
// Array size is determined by DISPLAY_ROWS from the hardware renderer
char displayText[DISPLAY_ROWS][MAX_TEXT_LENGTH] = {{0}};

// Spinlock for atomic buffer updates (ESP32 only)
#if defined(ESP32)
  portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
#endif

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
  if (row >= displayConfig.rows) return;  // 0-based indexing
  if (x >= displayConfig.panelsPerRow * displayConfig.panelWidth) return;
  if (y >= displayConfig.panelHeight) return;
  
  // Calculate pixel index in buffer
  // Each row occupies (panelsPerRow * panelWidth * panelHeight) pixels
  uint16_t rowPixels = displayConfig.panelsPerRow * displayConfig.panelWidth * displayConfig.panelHeight;
  uint16_t pixelIndex = row * rowPixels + y * (displayConfig.panelsPerRow * displayConfig.panelWidth) + x;
  
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
  
  // Use critical section to ensure atomic update (prevents MQTT from reading half-updated buffer)
  #if defined(ESP32)
    taskENTER_CRITICAL(&spinlock);
    memcpy(displayBuffer, renderBuffer, bufferSize);
    taskEXIT_CRITICAL(&spinlock);
  #elif defined(ESP8266)
    noInterrupts();
    memcpy(displayBuffer, renderBuffer, bufferSize);
    interrupts();
  #else
    // Fallback for other platforms
    memcpy(displayBuffer, renderBuffer, bufferSize);
  #endif
}

const uint8_t* getDisplayBuffer() {
  return displayBuffer;
}

uint16_t getDisplayBufferSize() {
  // Return actual buffer size needed for current configuration (in bytes)
  uint16_t totalPixels = displayConfig.rows * displayConfig.panelsPerRow * displayConfig.panelWidth * displayConfig.panelHeight;
  return (totalPixels + 7) / 8;
}
