#include "display.h"

// Display buffer storage
uint8_t displayBuffer[MAX_DISPLAY_BUFFER_SIZE / 8] = {0};
DisplayConfig displayConfig = {0};

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

void setPixel(uint8_t row, uint16_t x, uint16_t y, bool state) {
  if (row > displayConfig.rows || x >= displayConfig.panelsPerRow * displayConfig.panelWidth || y >= displayConfig.panelHeight) {
    return;
  }
  
  // Calculate pixel index:
  // 1. Row offset: skip all pixels from previous rows
  uint16_t rowWidth = displayConfig.panelsPerRow * displayConfig.panelWidth;
  uint16_t pixelsPerRow = rowWidth * displayConfig.panelHeight;
  uint16_t rowOffset = (row - 1) * pixelsPerRow;

  // 2. Within-row offset: y * row_width + x (standard row-major layout)
  uint16_t withinRowOffset = y * rowWidth + x;

  // 3. Total pixel index
  uint16_t pixelIndex = rowOffset + withinRowOffset;

  uint16_t byteIndex = pixelIndex / 8;
  uint8_t bitIndex = pixelIndex % 8;
  
  if (state) {
    displayBuffer[byteIndex] |= (1 << bitIndex);
  } else {
    displayBuffer[byteIndex] &= ~(1 << bitIndex);
  }
}

void clearDisplayBuffer() {
  memset(displayBuffer, 0, sizeof(displayBuffer));
}

const uint8_t* getDisplayBuffer() {
  return displayBuffer;
}

uint16_t getDisplayBufferSize() {
  // Return actual buffer size needed for current configuration (in bytes)
  return (displayConfig.numPixels + 7) / 8;
}

void updateDisplayContent(unsigned long counter) {
  // Clear display first
  clearDisplay();
  
  // Convert counter to string (max 8 digits for 16x16 panel)
  char counterStr[9];
  snprintf(counterStr, sizeof(counterStr), "%lu", counter);
  
  // Calculate starting X position to center the counter
  uint8_t numDigits = strlen(counterStr);
  uint8_t digitWidth = 6; // 5 pixels + 1 pixel spacing
  uint8_t totalWidth = numDigits * digitWidth - 1; // Remove spacing after last digit
  int16_t startX = (displayConfig.width - totalWidth) / 2;
  
  // Calculate starting Y position to center vertically
  int16_t startY = (displayConfig.height - 7) / 2;
  
  // Render each digit
  for (uint8_t i = 0; i < numDigits; i++) {
    uint8_t digit = counterStr[i] - '0';
    int16_t digitX = startX + (i * digitWidth);
    
    // Draw the digit using 5x7 font
    for (uint8_t col = 0; col < 5; col++) {
      for (uint8_t row = 0; row < 7; row++) {
        if (FONT_5X7[digit][col] & (1 << row)) {
          int16_t pixelX = digitX + col;
          int16_t pixelY = startY + row;
          
          // Only set pixel if within display bounds
          if (pixelX >= 0 && pixelX < displayConfig.width && 
              pixelY >= 0 && pixelY < displayConfig.height) {
            setPixel(pixelX, pixelY, true);
          }
        }
      }
    }
  }
}
