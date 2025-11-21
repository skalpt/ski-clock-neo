#include "display.h"

// Display buffer storage
uint8_t displayBuffer[MAX_DISPLAY_BUFFER_SIZE / 8] = {0};
DisplayConfig displayConfig = {0};

// 5x7 pixel font for digits 0-9
const uint8_t FONT_5X7[][7] = {
  {0x1F, 0x11, 0x11, 0x11, 0x1F}, // 0
  {0x00, 0x12, 0x1F, 0x10, 0x00}, // 1
  {0x1D, 0x15, 0x15, 0x15, 0x17}, // 2
  {0x11, 0x15, 0x15, 0x15, 0x1F}, // 3
  {0x07, 0x04, 0x04, 0x1F, 0x04}, // 4
  {0x17, 0x15, 0x15, 0x15, 0x1D}, // 5
  {0x1F, 0x15, 0x15, 0x15, 0x1D}, // 6
  {0x01, 0x01, 0x01, 0x01, 0x1F}, // 7
  {0x1F, 0x15, 0x15, 0x15, 0x1F}, // 8
  {0x17, 0x15, 0x15, 0x15, 0x1F}  // 9
};

void initDisplay(uint8_t rows, uint8_t cols) {
  displayConfig.rows = rows;
  displayConfig.cols = cols;
  displayConfig.width = cols * PANEL_WIDTH;
  displayConfig.height = rows * PANEL_HEIGHT;
  displayConfig.numPixels = displayConfig.width * displayConfig.height;
  
  clearDisplay();
}

DisplayConfig getDisplayConfig() {
  return displayConfig;
}

bool getPixel(uint16_t x, uint16_t y) {
  if (x >= displayConfig.width || y >= displayConfig.height) {
    return false;
  }
  
  uint16_t pixelIndex = y * displayConfig.width + x;
  uint16_t byteIndex = pixelIndex / 8;
  uint8_t bitIndex = pixelIndex % 8;
  
  return (displayBuffer[byteIndex] >> bitIndex) & 0x01;
}

void setPixel(uint16_t x, uint16_t y, bool state) {
  if (x >= displayConfig.width || y >= displayConfig.height) {
    return;
  }
  
  uint16_t pixelIndex = y * displayConfig.width + x;
  uint16_t byteIndex = pixelIndex / 8;
  uint8_t bitIndex = pixelIndex % 8;
  
  if (state) {
    displayBuffer[byteIndex] |= (1 << bitIndex);
  } else {
    displayBuffer[byteIndex] &= ~(1 << bitIndex);
  }
}

void clearDisplay() {
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
