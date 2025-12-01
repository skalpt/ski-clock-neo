#ifndef DISPLAY_CORE_H
#define DISPLAY_CORE_H

#include <Arduino.h>
#include "ski-clock-neo_config.h"

// Calculate total dimensions
#define ROW_WIDTH (PANELS_PER_ROW * PANEL_WIDTH)
#define ROW_HEIGHT PANEL_HEIGHT
#define DISPLAY_BUFFER_SIZE ((DISPLAY_ROWS * ROW_WIDTH * ROW_HEIGHT) / 8) // Bit-packed: (rows * width * height) / 8 bits per byte

#define MAX_TEXT_LENGTH 32

// Display configuration structure
struct DisplayConfig {
  uint8_t rows;           // Number of panel rows (1-2)
  uint8_t panelsPerRow;   // Number of panels per row (1-4)
  uint8_t panelWidth;     // Width of each panel in pixels
  uint8_t panelHeight;    // Height of each panel in pixels
};

// Display buffer - stores on/off state for each pixel (1 bit per pixel, packed into bytes)
extern uint8_t displayBuffer[DISPLAY_BUFFER_SIZE];
extern DisplayConfig displayConfig;

// Text content for each row (what should be displayed)
extern char displayText[DISPLAY_ROWS][MAX_TEXT_LENGTH];

// Initialize display system (includes hardware renderer and FreeRTOS/Ticker setup)
void initDisplay();

// Initialize display buffer with panel configuration
void initDisplayBuffer(uint8_t rows, uint8_t panelsPerRow, uint8_t panelWidth, uint8_t panelHeight);

// Get current display configuration
DisplayConfig getDisplayConfig();

// Set text content for a row (called by main firmware)
void setText(uint8_t row, const char* text);

// Set text content for a row without triggering render (for batch updates)
// Call triggerRender() after setting all rows to trigger a single render
// Returns true if text was actually changed
bool setTextNoRender(uint8_t row, const char* text);

// Trigger render after batch updates (notifies render task on ESP32)
void triggerRender();

// Get text content for a row (called by render libraries)
// WARNING: Not thread-safe for concurrent reads during setText()
// Use snapshotAllText() for atomic multi-row reads during rendering
const char* getText(uint8_t row);

// Atomically snapshot ALL row text into caller-provided buffer
// dest must be char[DISPLAY_ROWS][MAX_TEXT_LENGTH]
// This prevents torn reads when setText() and rendering overlap
void snapshotAllText(char dest[][MAX_TEXT_LENGTH]);

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

// Create snapshot buffer for MQTT publishing (proxies to hardware-specific renderer)
void createSnapshotBuffer();

// Event-driven rendering support
// Check if display needs to be re-rendered
bool isDisplayDirty();

// Clear the dirty flag (called after rendering)
void clearDirtyFlag();

// Check if rendering is requested (for main loop polling)
bool isRenderRequested();

// Clear the render request flag (called after invoking callback)
void clearRenderRequest();

// Get current update sequence number (for detecting concurrent setText() calls)
uint32_t getUpdateSequence();

// Atomically clear both flags ONLY if sequence hasn't changed since startSeq
// Returns true if flags were cleared, false if concurrent update detected
bool clearRenderFlagsIfUnchanged(uint32_t startSeq);

// Set a callback to be invoked when display needs rendering
// The callback will NOT be called directly from setText()
// Instead, the main loop must poll isRenderRequested() and call it
typedef void (*RenderCallback)();
void setRenderCallback(RenderCallback callback);

// Get the registered render callback
RenderCallback getRenderCallback();

// Force immediate rendering of current display state
void renderNow();

#endif
