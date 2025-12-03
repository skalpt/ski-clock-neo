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

char displayText[DISPLAY_ROWS][MAX_TEXT_LENGTH] = {{0}};

static volatile bool displayDirty = false;
static volatile bool renderRequested = false;
static volatile uint32_t updateSequence = 0;
static RenderCallback renderCallback = nullptr;

#if defined(ESP32)
  portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
  static TaskHandle_t displayTaskHandle = NULL;
#endif

void initDisplay() {
  displayConfig.rows = DISPLAY_ROWS;
  displayConfig.panelWidth = PANEL_WIDTH;
  displayConfig.panelHeight = PANEL_HEIGHT;
  
  uint16_t pixelOffset = 0;
  uint16_t totalPixels = 0;
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
    displayConfig.rowConfig[row].panels = 1;
    displayConfig.rowConfig[row].width = PANEL_WIDTH;
    displayConfig.rowConfig[row].height = PANEL_HEIGHT;
    displayConfig.rowConfig[row].pixelOffset = pixelOffset;
    
    uint16_t rowPixels = PANEL_WIDTH * PANEL_HEIGHT;
    pixelOffset += rowPixels;
    totalPixels += rowPixels;
  }
  displayConfig.totalPixels = totalPixels;
  displayConfig.bufferSize = (totalPixels + 7) / 8;
  
  clearDisplayBuffer();
  
  DEBUG_PRINTLN("Display core initialized");
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
  for (uint8_t row = 0; row < DISPLAY_ROWS; row++) {
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
}
