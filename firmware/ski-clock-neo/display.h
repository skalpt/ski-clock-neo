#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Maximum supported display configuration
#define MAX_DISPLAY_ROWS 2
#define MAX_DISPLAY_COLS 4
#define PANEL_WIDTH 16
#define PANEL_HEIGHT 16

// Maximum buffer size: 2 rows × 4 cols × 16×16 pixels = 2048 pixels
#define MAX_DISPLAY_BUFFER_SIZE (MAX_DISPLAY_ROWS * MAX_DISPLAY_COLS * PANEL_WIDTH * PANEL_HEIGHT)

// Display configuration structure
struct DisplayConfig {
  uint8_t rows;        // Number of panel rows (1-2)
  uint8_t cols;        // Number of panel columns (1-4)
  uint16_t width;      // Total display width in pixels
  uint16_t height;     // Total display height in pixels
  uint16_t numPixels;  // Total number of pixels
};

// Display buffer - stores on/off state for each pixel (1 bit per pixel, packed into bytes)
extern uint8_t displayBuffer[MAX_DISPLAY_BUFFER_SIZE / 8];
extern DisplayConfig displayConfig;

// Initialize display system with panel configuration
void initDisplay(uint8_t rows, uint8_t cols);

// Get current display configuration
DisplayConfig getDisplayConfig();

// Generate content to display (digit counter in this case)
void updateDisplayContent(unsigned long counter);

// Get pixel state from buffer (returns true if pixel is on, false if off)
bool getPixel(uint16_t x, uint16_t y);

// Set pixel state in buffer
void setPixel(uint16_t x, uint16_t y, bool state);

// Clear entire display buffer
void clearDisplay();

// Get display buffer pointer for direct access (used by MQTT snapshot)
const uint8_t* getDisplayBuffer();

// Get display buffer size in bytes
uint16_t getDisplayBufferSize();

#endif
