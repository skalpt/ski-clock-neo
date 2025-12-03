#ifndef NORRTEK_PRODUCT_CONFIG_H
#define NORRTEK_PRODUCT_CONFIG_H

#include <Arduino.h>

#define MAX_PRODUCT_NAME_LEN 32
#define MAX_DISPLAY_ROWS 4

struct ProductConfig {
  char productName[MAX_PRODUCT_NAME_LEN];
  
  uint8_t displayRows;
  uint8_t panelWidth;
  uint8_t panelHeight;
  uint8_t panelsPerRow[MAX_DISPLAY_ROWS];
  uint8_t displayPins[MAX_DISPLAY_ROWS];
  
  uint8_t displayColorR;
  uint8_t displayColorG;
  uint8_t displayColorB;
  uint8_t brightness;
  
  int8_t rtcSdaPin;
  int8_t rtcSclPin;
  int8_t temperaturePin;
  int8_t buttonPin;
};

typedef void (*DisplayUpdateCallback)();

typedef void (*MqttCommandCallback)(const char* command, const char* payload);

#endif
