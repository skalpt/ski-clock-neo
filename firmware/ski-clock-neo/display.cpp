#include "display.h"
#include "display_controller.h"
#include "neopixel_render.h"
#include "timer_task.h"
#include "debug.h"
#include <string.h>

// Display buffer storage (final rendered output for MQTT)
uint8_t displayBuffer[DISPLAY_BUFFER_SIZE] = {0};
DisplayConfig displayConfig = {0};

// Text content storage (what should be displayed on each row)
char displayText[DISPLAY_ROWS][MAX_TEXT_LENGTH] = {{0}};

// Event-driven rendering state  
static volatile bool displayDirty = false;
static volatile bool renderRequested = false;  // Flag for deferred rendering
static volatile uint32_t updateSequence = 0;  // Sequence counter to detect concurrent updates
static RenderCallback renderCallback = nullptr;

// Spinlock for atomic buffer updates (ESP32 only)
#if defined(ESP32)
  portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
  static TaskHandle_t displayTaskHandle = NULL;
#endif

// Forward declaration for display callback
void displayRenderCallback();

// Display rendering callback - shared logic for both platforms
// ESP32: Called from notification-based FreeRTOS task
// ESP8266: Called from 1ms timer (only when dirty flag is set)
void displayRenderCallback() {
  // Only render if display is dirty
  if (!isDisplayDirty()) {
    return;
  }
  
  // Drain all pending updates atomically
  while (isDisplayDirty()) {
    uint32_t startSeq = getUpdateSequence();
    
    // Render the display
    updateNeoPixels();
    
    // Clear flags atomically - if concurrent update occurred, flags stay set and loop continues
    bool flagsCleared = clearRenderFlagsIfUnchanged(startSeq);
    
    if (!flagsCleared) {
      // Concurrent setText() detected during render - flags still set, loop will continue
      DEBUG_PRINTLN("Concurrent setText() detected, re-rendering");
    }
    // If flags were cleared successfully, isDisplayDirty() will return false and loop exits
  }
}

#if defined(ESP32)
// Rendering task for ESP32 (blocks on task notification, woken immediately when display dirty)
void displayTask(void* parameter) {
  DEBUG_PRINTLN("Display FreeRTOS task started - waiting for notifications");
  
  for(;;) {
    // Block until notified (wake immediately when setText() marks dirty)
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Clear notification value on exit, wait forever
    
    // Use shared render callback
    displayRenderCallback();
  }
}
#endif

void initDisplay() {
  // Initialize display buffer with actual hardware configuration
  displayConfig.rows = DISPLAY_ROWS;
  displayConfig.panelsPerRow = PANELS_PER_ROW;
  displayConfig.panelWidth = PANEL_WIDTH;
  displayConfig.panelHeight = PANEL_HEIGHT;
  clearDisplayBuffer();

  // Initialize hardware renderer (NeoPixels)
  initNeoPixels();

  // Create display rendering task/timer using timer_task library
  #if defined(ESP32)
    // ESP32: Use notification-based task for immediate wake on setText()
    // Task blocks on ulTaskNotifyTake() until notified
    displayTaskHandle = createNotificationTask("Display", displayTask, 2048, 2);
  #elif defined(ESP8266)
    // ESP8266: Use 1ms polling timer (checks dirty flag each iteration)
    createTimer("Display", 1, displayRenderCallback, 0);
  #endif

  initDisplayController(); // Initialize controller to handle display logic
}

DisplayConfig getDisplayConfig() {
  return displayConfig;
}

void setText(uint8_t row, const char* text) {
  if (row >= displayConfig.rows) return;  // Dynamic row bounds check
  
  bool textChanged = false;
  
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
      textChanged = true;
    }
    portEXIT_CRITICAL(&spinlock);
    
    // Wake rendering task immediately (outside critical section)
    if (textChanged) {
      notifyTask(displayTaskHandle);  // Immediate wakeup, no delay!
    }
    
  #elif defined(ESP8266)
    noInterrupts();
    // Check if text changed (inside critical section to prevent torn reads)
    if (strcmp(displayText[row], text) != 0) {
      strncpy(displayText[row], text, MAX_TEXT_LENGTH - 1);
      displayText[row][MAX_TEXT_LENGTH - 1] = '\0';
      updateSequence++;
      displayDirty = true;
      renderRequested = true;
      textChanged = true;
    }
    interrupts();
    // TickTwo runs continuously and checks dirty flag - no trigger needed
    (void)textChanged;  // Suppress unused warning
    
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
  
  // NOTE: We do NOT call rendering functions directly here!
  // Instead, we wake the FreeRTOS task (ESP32) or trigger Ticker (ESP8266)
  // for immediate, non-blocking rendering with sub-millisecond latency.
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

void createSnapshotBuffer() {
  // Proxy to hardware-specific renderer
  // This abstraction allows easy swap to other display types (e.g., HUB75)
  createNeopixelSnapshot();
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

void renderNow() {
  updateNeoPixels(); // Force immediate render; don't wait for next tick
}