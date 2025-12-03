#include "display_core.h"
#include "../core/timer_helpers.h"
#include <string.h>

#if defined(ESP32)
  #define DISPLAY_ENTER_CRITICAL() portENTER_CRITICAL(&spinlock)
  #define DISPLAY_EXIT_CRITICAL() portEXIT_CRITICAL(&spinlock)
#else
  #define DISPLAY_ENTER_CRITICAL() noInterrupts()
  #define DISPLAY_EXIT_CRITICAL() interrupts()
#endif

uint8_t displayBuffer[MAX_DISPLAY_BUFFER_SIZE] = {0};
DisplayConfig displayConfig = {0};

static char displayText[MAX_ROWS][MAX_TEXT_LENGTH] = {{0}};

static volatile bool displayDirty = false;
static volatile bool renderRequested = false;
static volatile uint32_t updateSequence = 0;
static RenderCallback renderCallback = nullptr;

#if defined(ESP32)
  portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
  static TaskHandle_t displayTaskHandle = NULL;
#endif

void initDisplay() {
  DisplayInitConfig defaultConfig;
  defaultConfig.rows = 2;
  defaultConfig.panelWidth = 16;
  defaultConfig.panelHeight = 16;
  static const uint8_t defaultPanels[] = {1, 1, 1, 1};
  defaultConfig.panelsPerRow = defaultPanels;
  
  initDisplayWithConfig(defaultConfig);
}

void initDisplayWithConfig(const DisplayInitConfig& config) {
  displayConfig.rows = config.rows;
  displayConfig.panelWidth = config.panelWidth;
  displayConfig.panelHeight = config.panelHeight;
  
  uint16_t pixelOffset = 0;
  uint16_t totalPixels = 0;
  
  for (uint8_t row = 0; row < config.rows && row < MAX_ROWS; row++) {
    uint8_t panels = config.panelsPerRow ? config.panelsPerRow[row] : 1;
    displayConfig.rowConfig[row].panels = panels;
    displayConfig.rowConfig[row].width = panels * config.panelWidth;
    displayConfig.rowConfig[row].height = config.panelHeight;
    displayConfig.rowConfig[row].pixelOffset = pixelOffset;
    
    uint16_t rowPixels = displayConfig.rowConfig[row].width * config.panelHeight;
    pixelOffset += rowPixels;
    totalPixels += rowPixels;
    
    displayText[row][0] = '\0';
  }
  
  displayConfig.totalPixels = totalPixels;
  displayConfig.bufferSize = (totalPixels + 7) / 8;
  
  if (displayConfig.bufferSize > MAX_DISPLAY_BUFFER_SIZE) {
    DEBUG_PRINTLN("WARNING: Display buffer size exceeds maximum!");
    displayConfig.bufferSize = MAX_DISPLAY_BUFFER_SIZE;
  }
  
  clearDisplayBuffer();
  
  DEBUG_PRINT("Display initialized: ");
  DEBUG_PRINT(config.rows);
  DEBUG_PRINT(" rows, ");
  DEBUG_PRINT(config.panelWidth);
  DEBUG_PRINT("x");
  DEBUG_PRINT(config.panelHeight);
  DEBUG_PRINT(" panels, ");
  DEBUG_PRINT(totalPixels);
  DEBUG_PRINT(" pixels, ");
  DEBUG_PRINT(displayConfig.bufferSize);
  DEBUG_PRINTLN(" bytes buffer");
}

void setText(uint8_t row, const char* text) {
  if (row >= displayConfig.rows) return;
  
  bool textChanged = false;
  
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
    if (textChanged && displayTaskHandle != NULL) {
      xTaskNotifyGive(displayTaskHandle);
    }
  #else
    (void)textChanged;
  #endif
}

bool setTextNoRender(uint8_t row, const char* text) {
  if (row >= displayConfig.rows) return false;
  
  bool changed = false;
  DISPLAY_ENTER_CRITICAL();
  if (strcmp(displayText[row], text) != 0) {
    strncpy(displayText[row], text, MAX_TEXT_LENGTH - 1);
    displayText[row][MAX_TEXT_LENGTH - 1] = '\0';
    updateSequence++;
    displayDirty = true;
    renderRequested = true;
    changed = true;
  }
  DISPLAY_EXIT_CRITICAL();
  return changed;
}

void triggerRender() {
  #if defined(ESP32)
    if (displayTaskHandle != nullptr) {
      xTaskNotifyGive(displayTaskHandle);
    }
  #endif
}

const char* getText(uint8_t row) {
  if (row >= displayConfig.rows) return "";
  return displayText[row];
}

void snapshotAllText(char dest[][MAX_TEXT_LENGTH]) {
  DISPLAY_ENTER_CRITICAL();
  for (uint8_t row = 0; row < displayConfig.rows && row < MAX_ROWS; row++) {
    strncpy(dest[row], displayText[row], MAX_TEXT_LENGTH);
  }
  DISPLAY_EXIT_CRITICAL();
}

DisplayConfig getDisplayConfig() {
  return displayConfig;
}

void setPixel(uint8_t row, uint16_t x, uint16_t y, bool state) {
  if (row >= displayConfig.rows) return;
  
  RowConfig& rowCfg = displayConfig.rowConfig[row];
  if (x >= rowCfg.width) return;
  if (y >= rowCfg.height) return;
  
  uint16_t pixelIndex = rowCfg.pixelOffset + y * rowCfg.width + x;
  
  uint16_t byteIndex = pixelIndex / 8;
  uint8_t bitIndex = pixelIndex % 8;
  
  if (byteIndex >= displayConfig.bufferSize) return;
  
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
  
  DISPLAY_ENTER_CRITICAL();
  memcpy(displayBuffer, renderBuffer, bufferSize);
  DISPLAY_EXIT_CRITICAL();
}

const uint8_t* getDisplayBuffer() {
  return displayBuffer;
}

uint16_t getDisplayBufferSize() {
  return displayConfig.bufferSize;
}

void createSnapshotBuffer() {
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

RenderCallback getRenderCallback() {
  return renderCallback;
}

uint32_t getUpdateSequence() {
  return updateSequence;
}

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

void renderNow() {
  if (renderCallback) {
    renderCallback();
  }
}
