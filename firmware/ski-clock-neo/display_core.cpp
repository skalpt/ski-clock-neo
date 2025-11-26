// ============================================================================
// display_core.cpp - Hardware-agnostic display buffer and text management
// ============================================================================
// This library manages a bit-packed display buffer and text content for each
// row. It provides thread-safe text updates with atomic operations and
// event-driven rendering via FreeRTOS tasks (ESP32) or TickTwo timers (ESP8266).
// The actual pixel rendering is delegated to neopixel_render.h.
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "display_core.h"
#include "display_controller.h"
#include "neopixel_render.h"
#include "timing_helpers.h"
#include "debug.h"
#include <string.h>

// ============================================================================
// PLATFORM-SPECIFIC MACROS
// ============================================================================

#if defined(ESP32)
  #define DISPLAY_ENTER_CRITICAL() portENTER_CRITICAL(&spinlock)
  #define DISPLAY_EXIT_CRITICAL() portEXIT_CRITICAL(&spinlock)
#else
  #define DISPLAY_ENTER_CRITICAL() noInterrupts()
  #define DISPLAY_EXIT_CRITICAL() interrupts()
#endif

// ============================================================================
// STATE VARIABLES
// ============================================================================

// Display buffer storage (final rendered output for MQTT snapshots)
uint8_t displayBuffer[DISPLAY_BUFFER_SIZE] = {0};
DisplayConfig displayConfig = {0};

// Text content storage (what should be displayed on each row)
char displayText[DISPLAY_ROWS][MAX_TEXT_LENGTH] = {{0}};

// Event-driven rendering state
static volatile bool displayDirty = false;           // True when display needs re-rendering
static volatile bool renderRequested = false;        // Flag for deferred rendering
static volatile uint32_t updateSequence = 0;         // Sequence counter to detect concurrent updates
static RenderCallback renderCallback = nullptr;      // Optional callback after render

// Platform-specific synchronization primitives
#if defined(ESP32)
  portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;  // Spinlock for atomic buffer updates
  static TaskHandle_t displayTaskHandle = NULL;           // FreeRTOS task handle for rendering
#endif

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void displayRenderCallback();

// ============================================================================
// RENDER CALLBACKS (called by timer/task system)
// ============================================================================

// Display rendering callback - shared logic for both platforms
// ESP32: Called from notification-based FreeRTOS task
// ESP8266: Called from 1ms timer (only when dirty flag is set)
void displayRenderCallback() {
  // Only render if display is dirty
  if (!isDisplayDirty()) {
    return;
  }
  
  // Drain all pending updates atomically
  // This loop ensures we catch any setText() calls that happen during rendering
  while (isDisplayDirty()) {
    uint32_t startSeq = getUpdateSequence();
    
    // Render the display (delegates to neopixel_render.h)
    updateNeoPixels();
    
    // Clear flags atomically - if concurrent update occurred, flags stay set and loop continues
    bool flagsCleared = clearRenderFlagsIfUnchanged(startSeq);
    
    if (!flagsCleared) {
      // Concurrent setText() detected during render - flags still set, loop will continue
      DEBUG_PRINTLN("Concurrent setText() detected, re-rendering");
    }
  }
}

#if defined(ESP32)
// FreeRTOS rendering task for ESP32
// Blocks on task notification, woken immediately when display becomes dirty
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

// ============================================================================
// INITIALIZATION
// ============================================================================

void initDisplay() {
  // Initialize display buffer with actual hardware configuration
  displayConfig.rows = DISPLAY_ROWS;
  displayConfig.panelsPerRow = PANELS_PER_ROW;
  displayConfig.panelWidth = PANEL_WIDTH;
  displayConfig.panelHeight = PANEL_HEIGHT;
  clearDisplayBuffer();

  // Initialize hardware renderer (NeoPixels)
  initNeoPixels();

  // Create display rendering task/timer using timing_helpers library
  #if defined(ESP32)
    // ESP32: Use notification-based task for immediate wake on setText()
    // Task blocks on ulTaskNotifyTake() until notified
    displayTaskHandle = createNotificationTask("Display", displayTask, 2048, 2);
  #elif defined(ESP8266)
    // ESP8266: Use 1ms polling timer (checks dirty flag each iteration)
    createTimer("Display", 1, displayRenderCallback);
  #endif

  // Initialize display controller to handle content logic (time/date/temp)
  initDisplayController();
}

// ============================================================================
// TEXT MANAGEMENT (Public API)
// ============================================================================

// Set text for a specific row (thread-safe, triggers re-render)
void setText(uint8_t row, const char* text) {
  if (row >= displayConfig.rows) return;  // Bounds check
  
  bool textChanged = false;
  
  // All operations (compare, write, flag updates) must be atomic
  // to prevent torn reads from concurrent setText() or snapshotAllText()
  DISPLAY_ENTER_CRITICAL();
  if (strcmp(displayText[row], text) != 0) {
    strncpy(displayText[row], text, MAX_TEXT_LENGTH - 1);
    displayText[row][MAX_TEXT_LENGTH - 1] = '\0';
    updateSequence++;
    displayDirty = true;
    renderRequested = true;
    textChanged = true;
  }
  DISPLAY_EXIT_CRITICAL();
  
  #if defined(ESP32)
    // Wake rendering task immediately (outside critical section)
    if (textChanged) {
      notifyTask(displayTaskHandle);  // Immediate wakeup, no delay!
    }
  #else
    (void)textChanged;  // Suppress unused warning on ESP8266
  #endif
}

// Get text for a specific row (read-only)
const char* getText(uint8_t row) {
  if (row >= displayConfig.rows) return "";
  return displayText[row];
}

// Atomically copy all row text to destination buffer (prevents torn reads)
void snapshotAllText(char dest[][MAX_TEXT_LENGTH]) {
  DISPLAY_ENTER_CRITICAL();
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    strncpy(dest[row], displayText[row], MAX_TEXT_LENGTH);
  }
  DISPLAY_EXIT_CRITICAL();
}

// ============================================================================
// BUFFER MANAGEMENT (Public API)
// ============================================================================

// Get current display configuration
DisplayConfig getDisplayConfig() {
  return displayConfig;
}

// Set a single pixel in the display buffer
void setPixel(uint8_t row, uint16_t x, uint16_t y, bool state) {
  if (row >= displayConfig.rows) return;
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

// Clear the entire display buffer
void clearDisplayBuffer() {
  memset(displayBuffer, 0, sizeof(displayBuffer));
}

// Commit a render buffer to the display buffer (thread-safe)
void commitBuffer(const uint8_t* renderBuffer, uint16_t bufferSize) {
  if (bufferSize > sizeof(displayBuffer)) {
    bufferSize = sizeof(displayBuffer);
  }
  
  // Use critical section to ensure atomic update (prevents MQTT from reading half-updated buffer)
  DISPLAY_ENTER_CRITICAL();
  memcpy(displayBuffer, renderBuffer, bufferSize);
  DISPLAY_EXIT_CRITICAL();
}

// Get pointer to display buffer (for MQTT snapshots)
const uint8_t* getDisplayBuffer() {
  return displayBuffer;
}

// Get size of display buffer in bytes
uint16_t getDisplayBufferSize() {
  uint16_t totalPixels = displayConfig.rows * displayConfig.panelsPerRow * displayConfig.panelWidth * displayConfig.panelHeight;
  return (totalPixels + 7) / 8;
}

// ============================================================================
// SNAPSHOT & RENDER CONTROL (Public API)
// ============================================================================

// Create snapshot buffer on-demand (delegates to hardware renderer)
void createSnapshotBuffer() {
  createNeopixelSnapshot();
}

// Check if display needs re-rendering
bool isDisplayDirty() {
  return displayDirty;
}

// Clear the dirty flag
void clearDirtyFlag() {
  displayDirty = false;
}

// Check if a render has been requested
bool isRenderRequested() {
  return renderRequested;
}

// Clear the render request flag
void clearRenderRequest() {
  renderRequested = false;
}

// Set optional render callback
void setRenderCallback(RenderCallback callback) {
  renderCallback = callback;
}

// Get current update sequence number
uint32_t getUpdateSequence() {
  return updateSequence;
}

// Atomically clear render flags if sequence unchanged (returns true if cleared)
bool clearRenderFlagsIfUnchanged(uint32_t startSeq) {
  bool cleared = false;
  
    DISPLAY_ENTER_CRITICAL();
    if (updateSequence == startSeq) {
      displayDirty = false;
      renderRequested = false;
      cleared = true;
    }
    DISPLAY_EXIT_CRITICAL();
  
  return cleared;
}

// Get optional render callback
RenderCallback getRenderCallback() {
  return renderCallback;
}

// Force immediate render (bypasses dirty flag check)
void renderNow() {
  DISPLAY_ENTER_CRITICAL();
  updateNeoPixels();
  DISPLAY_EXIT_CRITICAL();
}