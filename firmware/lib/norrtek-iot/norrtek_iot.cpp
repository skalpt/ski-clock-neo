#include "norrtek_iot.h"

ProductConfig norrtekConfig;
DisplayUpdateCallback displayUpdateCallback = nullptr;
MqttCommandCallback mqttCommandCallback = nullptr;

static String generateSnapshotPayload() {
  DisplayConfig cfg = getDisplayConfig();
  
  if (cfg.rows == 0 || cfg.totalPixels == 0) {
    DEBUG_PRINTLN("Invalid display configuration, skipping snapshot");
    return "";
  }
  
  createSnapshotBuffer();
  
  const uint8_t* buffer = getDisplayBuffer();
  uint16_t bufferSize = getDisplayBufferSize();
  
  if (bufferSize == 0 || bufferSize > 1024) {
    DEBUG_PRINTLN("Invalid buffer size, skipping snapshot");
    return "";
  }
  
  String payload = "{\"product\":\"";
  payload += norrtekConfig.productName;
  payload += "\",\"rows\":[";
  
  for (uint8_t i = 0; i < cfg.rows; i++) {
    if (i > 0) payload += ",";
    
    RowConfig& rowCfg = cfg.rowConfig[i];
    const char* text = getText(i);
    
    uint16_t rowPixels = rowCfg.width * rowCfg.height;
    uint16_t startBit = rowCfg.pixelOffset;
    uint16_t startByte = startBit / 8;
    uint16_t rowBytes = (rowPixels + 7) / 8;
    
    String rowBase64 = base64Encode(buffer + startByte, rowBytes);
    
    String escapedText = "";
    for (int j = 0; text[j] != '\0' && j < MAX_TEXT_LENGTH; j++) {
      if (text[j] == '"' || text[j] == '\\') {
        escapedText += "\\";
      }
      escapedText += text[j];
    }
    
    payload += "{\"text\":\"" + escapedText + "\"";
    payload += ",\"cols\":" + String(rowCfg.panels);
    payload += ",\"width\":" + String(rowCfg.width);
    payload += ",\"height\":" + String(rowCfg.height);
    payload += ",\"mono\":\"" + rowBase64 + "\"";
    payload += "}";
  }
  
  payload += "]}";
  
  return payload;
}

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
  
  setSnapshotPayloadCallback(generateSnapshotPayload);
  
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
