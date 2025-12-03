#include "ota_update.h"
#include "mqtt_client.h"
#include "../core/led_indicator.h"

static char otaProductName[32] = "generic";

bool otaUpdateInProgress = false;

void setOtaProduct(const char* productName) {
  strncpy(otaProductName, productName, sizeof(otaProductName) - 1);
  otaProductName[sizeof(otaProductName) - 1] = '\0';
}

void publishOTAStart(String newVersion) {
  static char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"product\":\"%s\",\"platform\":\"%s\",\"old_version\":\"%s\",\"new_version\":\"%s\"}",
    otaProductName,
    getPlatform().c_str(),
    FIRMWARE_VERSION,
    newVersion.c_str()
  );
  
  publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_OTA_START), payload);
}

void publishOTAProgress(int progress) {
  static char payload[32];
  snprintf(payload, sizeof(payload), "{\"progress\":%d}", progress);
  
  publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_OTA_PROGRESS), payload);
}

void publishOTAComplete(bool success, String errorMessage) {
  static char payload[256];
  if (success) {
    snprintf(payload, sizeof(payload), "{\"status\":\"success\"}");
  } else {
    snprintf(payload, sizeof(payload),
      "{\"status\":\"failed\",\"error\":\"%s\"}",
      errorMessage.c_str()
    );
  }
  
  publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_OTA_COMPLETE), payload);
}

bool performOTAUpdate(String version) {
  if (!WiFi.isConnected()) {
    DEBUG_PRINTLN("OTA: WiFi not connected");
    publishOTAComplete(false, "WiFi not connected");
    return false;
  }

  beginLedOverride(LED_OTA_PROGRESS);
  
  String binaryUrl = String(UPDATE_SERVER_URL) + "/api/firmware/" + getPlatform();
  
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("OTA UPDATE STARTING");
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINT("Product: ");
  DEBUG_PRINTLN(otaProductName);
  DEBUG_PRINT("Current version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);
  DEBUG_PRINT("New version: ");
  DEBUG_PRINTLN(version);
  DEBUG_PRINT("Download URL: ");
  DEBUG_PRINTLN(binaryUrl);
  DEBUG_PRINTLN("===========================================");
  
  publishOTAStart(version);
  otaUpdateInProgress = true;
  
#if defined(ESP32)
  HTTPClient http;
  bool isHttps = binaryUrl.startsWith("https://");
  
  WiFiClientSecure* secureClientPtr = nullptr;
  WiFiClient* plainClientPtr = nullptr;
  WiFiClient* clientPtr = nullptr;
  
  if (isHttps) {
    secureClientPtr = new WiFiClientSecure();
    secureClientPtr->setInsecure();
    clientPtr = secureClientPtr;
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
      publishOTAComplete(false, "Failed to begin HTTP connection");
      delete secureClientPtr;
      endLedOverride();
      otaUpdateInProgress = false;
      return false;
    }
  } else {
    plainClientPtr = new WiFiClient();
    clientPtr = plainClientPtr;
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
      publishOTAComplete(false, "Failed to begin HTTP connection");
      delete plainClientPtr;
      endLedOverride();
      otaUpdateInProgress = false;
      return false;
    }
  }
  
  http.addHeader("X-API-Key", DOWNLOAD_API_KEY);
  http.addHeader("User-Agent", "NorrtekIoT-OTA");
  
  DEBUG_PRINTLN("Starting ESP32 OTA download...");
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    DEBUG_PRINT("HTTP GET failed: ");
    DEBUG_PRINTLN(httpCode);
    publishOTAComplete(false, "HTTP GET failed");
    http.end();
    if (secureClientPtr) delete secureClientPtr;
    if (plainClientPtr) delete plainClientPtr;
    endLedOverride();
    otaUpdateInProgress = false;
    return false;
  }
  
  int contentLength = http.getSize();
  DEBUG_PRINT("Firmware size: ");
  DEBUG_PRINTLN(contentLength);
  
  if (contentLength <= 0) {
    DEBUG_PRINTLN("Invalid content length");
    publishOTAComplete(false, "Invalid content length");
    http.end();
    if (secureClientPtr) delete secureClientPtr;
    if (plainClientPtr) delete plainClientPtr;
    endLedOverride();
    otaUpdateInProgress = false;
    return false;
  }
  
  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    DEBUG_PRINTLN("Not enough space for OTA");
    publishOTAComplete(false, "Not enough space for OTA");
    http.end();
    if (secureClientPtr) delete secureClientPtr;
    if (plainClientPtr) delete plainClientPtr;
    endLedOverride();
    otaUpdateInProgress = false;
    return false;
  }
  
  WiFiClient * stream = http.getStreamPtr();
  
  uint8_t buff[512] = { 0 };
  size_t written = 0;
  int lastReportedProgress = 0;
  
  while (http.connected() && (written < (size_t)contentLength)) {
    size_t available = stream->available();
    if (available) {
      size_t bytesToRead = ((available > sizeof(buff)) ? sizeof(buff) : available);
      size_t bytesRead = stream->readBytes(buff, bytesToRead);
      
      size_t bytesWritten = Update.write(buff, bytesRead);
      if (bytesWritten != bytesRead) {
        DEBUG_PRINTLN("Write error during OTA");
        publishOTAComplete(false, "Write error during OTA");
        http.end();
        if (secureClientPtr) delete secureClientPtr;
        if (plainClientPtr) delete plainClientPtr;
        endLedOverride();
        otaUpdateInProgress = false;
        return false;
      }
      
      written += bytesWritten;
      
      int progress = (written * 100) / contentLength;
      if (progress >= lastReportedProgress + 10) {
        publishOTAProgress(progress);
        lastReportedProgress = progress;
      }
    }
    delay(1);
  }
  
  if (written == (size_t)contentLength) {
    DEBUG_PRINTLN("Firmware written successfully");
    publishOTAProgress(100);
  } else {
    DEBUG_PRINT("Written only ");
    DEBUG_PRINT(written);
    DEBUG_PRINT(" / ");
    DEBUG_PRINTLN(contentLength);
    publishOTAComplete(false, "Incomplete download");
    http.end();
    if (secureClientPtr) delete secureClientPtr;
    if (plainClientPtr) delete plainClientPtr;
    endLedOverride();
    otaUpdateInProgress = false;
    return false;
  }
  
  if (Update.end()) {
    if (Update.isFinished()) {
      DEBUG_PRINTLN("OTA Update successful! Rebooting...");
      publishOTAComplete(true);
      http.end();
      if (secureClientPtr) delete secureClientPtr;
      if (plainClientPtr) delete plainClientPtr;
      delay(2000);
      ESP.restart();
      return true;
    } else {
      DEBUG_PRINTLN("Update not finished");
      endLedOverride();
      otaUpdateInProgress = false;
      publishOTAComplete(false, "Update not finished");
    }
  } else {
    String errorMsg = Update.errorString();
    DEBUG_PRINT("Update error: ");
    DEBUG_PRINTLN(errorMsg);
    publishOTAComplete(false, errorMsg);
  }
  
  http.end();
  if (secureClientPtr) delete secureClientPtr;
  if (plainClientPtr) delete plainClientPtr;
  endLedOverride();
  otaUpdateInProgress = false;
  return false;
  
#elif defined(ESP8266)
  HTTPClient http;
  bool isHttps = binaryUrl.startsWith("https://");
  
  WiFiClientSecure* secureClientPtr = nullptr;
  WiFiClient* plainClientPtr = nullptr;
  WiFiClient* clientPtr = nullptr;
  
  if (isHttps) {
    secureClientPtr = new WiFiClientSecure();
    secureClientPtr->setInsecure();
    clientPtr = secureClientPtr;
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
      publishOTAComplete(false, "Failed to begin HTTP connection");
      delete secureClientPtr;
      endLedOverride();
      otaUpdateInProgress = false;
      return false;
    }
  } else {
    plainClientPtr = new WiFiClient();
    clientPtr = plainClientPtr;
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
      publishOTAComplete(false, "Failed to begin HTTP connection");
      delete plainClientPtr;
      endLedOverride();
      otaUpdateInProgress = false;
      return false;
    }
  }
  
  http.addHeader("X-API-Key", DOWNLOAD_API_KEY);
  http.addHeader("User-Agent", "NorrtekIoT-OTA");
  
  DEBUG_PRINTLN("Starting ESP8266 OTA download...");
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    DEBUG_PRINT("HTTP GET failed: ");
    DEBUG_PRINTLN(httpCode);
    publishOTAComplete(false, "HTTP GET failed");
    http.end();
    if (secureClientPtr) delete secureClientPtr;
    if (plainClientPtr) delete plainClientPtr;
    endLedOverride();
    otaUpdateInProgress = false;
    return false;
  }
  
  int contentLength = http.getSize();
  DEBUG_PRINT("Firmware size: ");
  DEBUG_PRINTLN(contentLength);
  
  if (contentLength <= 0) {
    DEBUG_PRINTLN("Invalid content length");
    publishOTAComplete(false, "Invalid content length");
    http.end();
    if (secureClientPtr) delete secureClientPtr;
    if (plainClientPtr) delete plainClientPtr;
    endLedOverride();
    otaUpdateInProgress = false;
    return false;
  }
  
  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    DEBUG_PRINTLN("Not enough space for OTA");
    publishOTAComplete(false, "Not enough space for OTA");
    http.end();
    if (secureClientPtr) delete secureClientPtr;
    if (plainClientPtr) delete plainClientPtr;
    endLedOverride();
    otaUpdateInProgress = false;
    return false;
  }
  
  WiFiClient * stream = http.getStreamPtr();
  
  uint8_t buff[512] = { 0 };
  size_t written = 0;
  int lastReportedProgress = 0;
  
  while (http.connected() && (written < (size_t)contentLength)) {
    size_t available = stream->available();
    if (available) {
      size_t bytesToRead = ((available > sizeof(buff)) ? sizeof(buff) : available);
      size_t bytesRead = stream->readBytes(buff, bytesToRead);
      
      size_t bytesWritten = Update.write(buff, bytesRead);
      if (bytesWritten != bytesRead) {
        DEBUG_PRINTLN("Write error during OTA");
        publishOTAComplete(false, "Write error during OTA");
        http.end();
        if (secureClientPtr) delete secureClientPtr;
        if (plainClientPtr) delete plainClientPtr;
        endLedOverride();
        otaUpdateInProgress = false;
        return false;
      }
      
      written += bytesWritten;
      
      int progress = (written * 100) / contentLength;
      if (progress >= lastReportedProgress + 10) {
        publishOTAProgress(progress);
        lastReportedProgress = progress;
      }
    }
    delay(1);
  }
  
  if (written == (size_t)contentLength) {
    DEBUG_PRINTLN("Firmware written successfully");
    publishOTAProgress(100);
  } else {
    DEBUG_PRINT("Written only ");
    DEBUG_PRINT(written);
    DEBUG_PRINT(" / ");
    DEBUG_PRINTLN(contentLength);
    publishOTAComplete(false, "Incomplete download");
    http.end();
    if (secureClientPtr) delete secureClientPtr;
    if (plainClientPtr) delete plainClientPtr;
    endLedOverride();
    otaUpdateInProgress = false;
    return false;
  }
  
  if (Update.end()) {
    if (Update.isFinished()) {
      DEBUG_PRINTLN("OTA Update successful! Rebooting...");
      publishOTAComplete(true);
      http.end();
      if (secureClientPtr) delete secureClientPtr;
      if (plainClientPtr) delete plainClientPtr;
      delay(2000);
      ESP.restart();
      return true;
    } else {
      DEBUG_PRINTLN("Update not finished");
      publishOTAComplete(false, "Update not finished");
    }
  } else {
    String errorMsg = Update.getErrorString();
    DEBUG_PRINT("Update error: ");
    DEBUG_PRINTLN(errorMsg);
    publishOTAComplete(false, errorMsg);
  }
  
  http.end();
  if (secureClientPtr) delete secureClientPtr;
  if (plainClientPtr) delete plainClientPtr;
  endLedOverride();
  otaUpdateInProgress = false;
  return false;
#endif
  
  return false;
}

void triggerOTAUpdate(String newVersion) {
  if (otaUpdateInProgress) {
    DEBUG_PRINTLN("OTA update already in progress");
    return;
  }
  
  if (!WiFi.isConnected()) {
    DEBUG_PRINTLN("OTA: WiFi not connected");
    return;
  }
  
  long currentVer = parseVersion(FIRMWARE_VERSION);
  long latestVer = parseVersion(newVersion);
  
  DEBUG_PRINT("Current version code: ");
  DEBUG_PRINTLN(currentVer);
  DEBUG_PRINT("Latest version code: ");
  DEBUG_PRINTLN(latestVer);
  
  if (latestVer > currentVer) {
    DEBUG_PRINTLN("New version available! Starting OTA update...");
    performOTAUpdate(newVersion);
  } else {
    DEBUG_PRINTLN("Firmware is up to date");
  }
}
