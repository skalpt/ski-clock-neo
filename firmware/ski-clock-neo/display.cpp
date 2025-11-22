// Include hardware config first to define DISPLAY_ROWS and DISPLAY_BUFFER_SIZE
#include "display_config.h"
#include "display.h"
#include <string.h>

// Display buffer storage (final rendered output for MQTT)
// Size is determined by DISPLAY_BUFFER_SIZE from the hardware renderer
uint8_t displayBuffer[DISPLAY_BUFFER_SIZE] = {0};
DisplayConfig displayConfig = {0};

// Text content storage (what should be displayed on each row)
// Array size is determined by DISPLAY_ROWS from the hardware renderer
char displayText[DISPLAY_ROWS][MAX_TEXT_LENGTH] = {{0}};

// Event-driven rendering state  
static volatile bool displayDirty = false;
static volatile bool renderRequested = false;  // Flag for deferred rendering
static volatile uint32_t updateSequence = 0;  // Sequence counter to detect concurrent updates
static RenderCallback renderCallback = nullptr;

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
  if (row >= displayConfig.rows) return;  // Dynamic row bounds check
  
  // All operations (compare, write, flag updates) must be atomic
  // to prevent torn reads from concurrent setText() or snapshotAllText()
  #if defined(ESP32)
    portENTER_CRITICAL(&spinlock);
    // Check if text changed (inside critical section to prevent torn reads)
    if (strcmp(displayText[row], text) != 0) {
      strncpy(displayText[row], text, MAX_TEXT_LENGTH - 1);
      displayText[row][MAX_TEXT_LENGTH - 1] = '\0';
      updateSequence++;
      displayDirty = true;
      renderRequested = true;
    }
    portEXIT_CRITICAL(&spinlock);
  #elif defined(ESP8266)
    noInterrupts();
    // Check if text changed (inside critical section to prevent torn reads)
    if (strcmp(displayText[row], text) != 0) {
      strncpy(displayText[row], text, MAX_TEXT_LENGTH - 1);
      displayText[row][MAX_TEXT_LENGTH - 1] = '\0';
      updateSequence++;
      displayDirty = true;
      renderRequested = true;
    }
    interrupts();
  #else
    // Fallback for other platforms (no atomic guarantee)
    if (strcmp(displayText[row], text) != 0) {
      strncpy(displayText[row], text, MAX_TEXT_LENGTH - 1);
      displayText[row][MAX_TEXT_LENGTH - 1] = '\0';
      updateSequence++;
      displayDirty = true;
      renderRequested = true;
    }
  #endif
  
  // NOTE: We do NOT call renderCallback directly here!
  // Calling heavy rendering functions from setText() is dangerous
  // because setText() can be called from ISR/timer/MQTT contexts.
  // Instead, the main loop polls isRenderRequested() and calls the callback.
}

const char* getText(uint8_t row) {
  if (row >= displayConfig.rows) return "";  // Dynamic row bounds check
  return displayText[row];
}

void snapshotAllText(char dest[][MAX_TEXT_LENGTH]) {
  // Atomically copy all row text to prevent torn reads during rendering
  #if defined(ESP32)
    portENTER_CRITICAL(&spinlock);
    for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
      strncpy(dest[row], displayText[row], MAX_TEXT_LENGTH);
    }
    portEXIT_CRITICAL(&spinlock);
  #elif defined(ESP8266)
    noInterrupts();
    for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
      strncpy(dest[row], displayText[row], MAX_TEXT_LENGTH);
    }
    interrupts();
  #else
    // Fallback for other platforms (no atomic guarantee)
    for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
      strncpy(dest[row], displayText[row], MAX_TEXT_LENGTH);
    }
  #endif
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

bool isDisplayDirty() {
  return displayDirty;
}

void clearDirtyFlag() {
  displayDirty = false;
}

bool isRenderRequested() {
  return renderRequested;
}

void clearRenderRequest() {
  renderRequested = false;
}

void setRenderCallback(RenderCallback callback) {
  renderCallback = callback;
}

uint32_t getUpdateSequence() {
  return updateSequence;
}

bool clearRenderFlagsIfUnchanged(uint32_t startSeq) {
  bool cleared = false;
  
  // Use critical section for atomic check-and-clear
  #if defined(ESP32)
    portENTER_CRITICAL(&spinlock);
    if (updateSequence == startSeq) {
      // No concurrent updates - safe to clear both flags
      displayDirty = false;
      renderRequested = false;
      cleared = true;
    }
    portEXIT_CRITICAL(&spinlock);
  #elif defined(ESP8266)
    noInterrupts();
    if (updateSequence == startSeq) {
      // No concurrent updates - safe to clear both flags
      displayDirty = false;
      renderRequested = false;
      cleared = true;
    }
    interrupts();
  #else
    // Fallback for other platforms (no atomic guarantee)
    if (updateSequence == startSeq) {
      displayDirty = false;
      renderRequested = false;
      cleared = true;
    }
  #endif
  
  return cleared;
}

RenderCallback getRenderCallback() {
  return renderCallback;
}
