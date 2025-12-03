#include "norrtek_iot.h"

ProductConfig norrtekConfig;
DisplayUpdateCallback displayUpdateCallback = nullptr;
MqttCommandCallback mqttCommandCallback = nullptr;

void initNorrtekIoT(const ProductConfig& config) {
  norrtekConfig = config;
  
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("\n\n===========================================");
  DEBUG_PRINT("Norrtek IoT - ");
  DEBUG_PRINTLN(config.productName);
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINT("Firmware version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);
  
  setMqttProduct(config.productName);
  setOtaProduct(config.productName);
  setWifiProduct(config.productName);
  
  initEventLog();
  logBootEvent();
  
  initLedIndicator();
  
  DisplayInitConfig displayInitConfig;
  displayInitConfig.rows = config.displayRows;
  displayInitConfig.panelWidth = config.panelWidth;
  displayInitConfig.panelHeight = config.panelHeight;
  displayInitConfig.panelsPerRow = (const uint8_t*)config.panelsPerRow;
  initDisplayWithConfig(displayInitConfig);
  
  initWiFi();
  
  initMQTT();
  
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Norrtek IoT initialized - entering main loop");
  DEBUG_PRINTLN("===========================================\n");
}

void processNorrtekIoT() {
  updateWiFi();
  
  updateMQTT();
  
  updateTimers();
}

void setDisplayUpdateCallback(DisplayUpdateCallback callback) {
  displayUpdateCallback = callback;
}

void setMqttCommandCallback(MqttCommandCallback callback) {
  mqttCommandCallback = callback;
}

const ProductConfig& getProductConfig() {
  return norrtekConfig;
}
