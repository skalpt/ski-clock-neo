#ifndef NORRTEK_DISPLAY_CORE_H
#define NORRTEK_DISPLAY_CORE_H

#include <Arduino.h>
#include <stdint.h>
#include "../core/debug.h"

#ifndef MAX_TEXT_LENGTH
  #define MAX_TEXT_LENGTH 32
#endif

#ifndef MAX_DISPLAY_BUFFER_SIZE
  #define MAX_DISPLAY_BUFFER_SIZE 512
#endif

#ifndef MAX_ROWS
  #define MAX_ROWS 4
#endif

struct RowConfig {
  uint8_t panels;
  uint16_t width;
  uint16_t height;
  uint16_t pixelOffset;
};

struct DisplayConfig {
  uint8_t rows;
  uint16_t panelWidth;
  uint16_t panelHeight;
  uint16_t totalPixels;
  uint16_t bufferSize;
  RowConfig rowConfig[MAX_ROWS];
};

struct DisplayInitConfig {
  uint8_t rows;
  uint8_t panelWidth;
  uint8_t panelHeight;
  const uint8_t* panelsPerRow;
};

typedef void (*RenderCallback)();

void initDisplay();
void initDisplayWithConfig(const DisplayInitConfig& config);

void setText(uint8_t row, const char* text);
bool setTextNoRender(uint8_t row, const char* text);
void triggerRender();
const char* getText(uint8_t row);
void snapshotAllText(char dest[][MAX_TEXT_LENGTH]);

DisplayConfig getDisplayConfig();
void setPixel(uint8_t row, uint16_t x, uint16_t y, bool state);
void clearDisplayBuffer();
void commitBuffer(const uint8_t* renderBuffer, uint16_t bufferSize);
const uint8_t* getDisplayBuffer();
uint16_t getDisplayBufferSize();

void createSnapshotBuffer();
bool isDisplayDirty();
void clearDirtyFlag();
bool isRenderRequested();
void clearRenderRequest();
void setRenderCallback(RenderCallback callback);
RenderCallback getRenderCallback();
uint32_t getUpdateSequence();
bool clearRenderFlagsIfUnchanged(uint32_t startSeq);
void renderNow();

extern uint8_t displayBuffer[];
extern DisplayConfig displayConfig;

#endif
