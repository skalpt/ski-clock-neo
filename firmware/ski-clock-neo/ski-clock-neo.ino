#include <norrtek_iot.h>
#include "ski-clock-neo_config.h"
#include "src/display/display_controller.h"
#include "src/display/fastled_render.h"

void setup() {
  ProductConfig config;
  
  strncpy(config.productName, "ski-clock-neo", MAX_PRODUCT_NAME_LEN - 1);
  config.productName[MAX_PRODUCT_NAME_LEN - 1] = '\0';
  
  config.displayRows = DISPLAY_ROWS;
  config.panelWidth = PANEL_WIDTH;
  config.panelHeight = PANEL_HEIGHT;
  
  for (uint8_t i = 0; i < DISPLAY_ROWS && i < MAX_DISPLAY_ROWS; i++) {
    config.panelsPerRow[i] = PANELS_PER_ROW[i];
    config.displayPins[i] = DISPLAY_PINS[i];
  }
  
  config.displayColorR = DISPLAY_COLOR_R;
  config.displayColorG = DISPLAY_COLOR_G;
  config.displayColorB = DISPLAY_COLOR_B;
  config.brightness = BRIGHTNESS;
  
  config.rtcSdaPin = RTC_SDA_PIN;
  config.rtcSclPin = RTC_SCL_PIN;
  config.temperaturePin = TEMPERATURE_PIN;
  config.buttonPin = BUTTON_PIN;
  
  initNorrtekIoT(config);
  
  initNeoPixels();
  
  initDisplayController();
  
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Ski Clock Neo ready");
  DEBUG_PRINTLN("===========================================\n");
}

void loop() {
  processNorrtekIoT();
  
  #if defined(ESP8266)
    updateTimers();
  #endif
}
