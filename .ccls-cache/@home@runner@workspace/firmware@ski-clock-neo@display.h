#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Maximum buffer size: 2 rows × 2 cols × 32×64 pixels = 8192 pixels
#define MAX_DISPLAY_BUFFER_SIZE (8192)
#define MAX_TEXT_LENGTH 32

// Display configuration structure
struct DisplayConfig {
  uint8_t rows;           // Number of panel rows (1-2)
  uint8_t panelsPerRow;   // Number of panels per row (1-4)
  uint8_t panelWidth;     // Width of each panel in pixels
  uint8_t panelHeight;    // Height of each panel in pixels
};

// Display buffer - stores on/off state for each pixel (1 bit per pixel, packed into bytes)
extern uint8_t displayBuffer[MAX_DISPLAY_BUFFER_SIZE / 8];
extern DisplayConfig displayConfig;

// Text content for each row (what should be displayed)
extern char displayText[2][MAX_TEXT_LENGTH];

// Initialize display system with panel configuration
void initDisplayBuffer(uint8_t rows, uint8_t panelsPerRow, uint8_t panelWidth, uint8_t panelHeight);

// Get current display configuration
DisplayConfig getDisplayConfig();

// Set text content for a row (called by main firmware)
void setText(uint8_t row, const char* text);

// Get text content for a row (called by render libraries)
const char* getText(uint8_t row);

// Set pixel state in buffer (called by render libraries during frame construction)
void setPixel(uint8_t row, uint16_t x, uint16_t y, bool state);

// Clear entire display buffer
void clearDisplayBuffer();

// Commit complete rendered frame to display buffer (called by render libraries)
// This atomically updates the buffer that MQTT reads from
void commitBuffer(const uint8_t* renderBuffer, uint16_t bufferSize);

// Get display buffer pointer for direct access (used by MQTT snapshot)
const uint8_t* getDisplayBuffer();

// Get display buffer size in bytes
uint16_t getDisplayBufferSize();

#endif
