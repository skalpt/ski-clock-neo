#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Import hardware configuration (defines DISPLAY_ROWS, DISPLAY_BUFFER_SIZE, etc.)
#include "display_config.h"

// Sanity checks to ensure config was loaded correctly
#ifndef DISPLAY_ROWS
  #error "DISPLAY_ROWS must be defined in display_config.h"
#endif

#ifndef DISPLAY_BUFFER_SIZE
  #error "DISPLAY_BUFFER_SIZE must be defined in display_config.h"
#endif

#define MAX_TEXT_LENGTH 32

// Display configuration structure
struct DisplayConfig {
  uint8_t rows;           // Number of panel rows (1-2)
  uint8_t panelsPerRow;   // Number of panels per row (1-4)
  uint8_t panelWidth;     // Width of each panel in pixels
  uint8_t panelHeight;    // Height of each panel in pixels
};

// Display buffer - stores on/off state for each pixel (1 bit per pixel, packed into bytes)
// Size is determined by DISPLAY_BUFFER_SIZE from the hardware renderer
extern uint8_t displayBuffer[DISPLAY_BUFFER_SIZE];
extern DisplayConfig displayConfig;

// Text content for each row (what should be displayed)
// Array size is determined by DISPLAY_ROWS from the hardware renderer
extern char displayText[DISPLAY_ROWS][MAX_TEXT_LENGTH];

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

// Event-driven rendering support
// Check if display needs to be re-rendered
bool isDisplayDirty();

// Clear the dirty flag (called after rendering)
void clearDirtyFlag();

// Check if rendering is requested (for main loop polling)
bool isRenderRequested();

// Clear the render request flag (called after invoking callback)
void clearRenderRequest();

// Set a callback to be invoked when display needs rendering
// The callback will NOT be called directly from setText()
// Instead, the main loop must poll isRenderRequested() and call it
typedef void (*RenderCallback)();
void setRenderCallback(RenderCallback callback);

// Get the registered render callback
RenderCallback getRenderCallback();

#endif
