// ============================================================================
// ota_update.cpp - Over-the-Air firmware update handling
// ============================================================================
// This library handles OTA firmware updates:
// - Secure HTTPS downloads from update server
// - API key authentication
// - Progress reporting via MQTT
// - LED indication during update
// - Supports both ESP32 and ESP8266
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "ota_update.h"               // This file's header
#include "../../ski-clock-neo_config.h" // For PRODUCT_NAME
#include "../core/led_indicator.h"    // For LED status patterns when OTA is in progress
#include "mqtt_client.h"              // For publishing OTA progress to MQTT

// ============================================================================
// STATE VARIABLES
// ============================================================================

bool otaUpdateInProgress = false;

// ============================================================================
// MQTT PROGRESS REPORTING
// ============================================================================

// Publish OTA start message (to device-specific topic)
void publishOTAStart(String newVersion) {
  static char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"product\":\"%s\",\"platform\":\"%s\",\"old_version\":\"%s\",\"new_version\":\"%s\"}",
    PRODUCT_NAME,
    getPlatform().c_str(),
    FIRMWARE_VERSION,
    newVersion.c_str()
  );
  
  publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_OTA_START), payload);
}

// Publish OTA progress message (0-100%, to device-specific topic)
void publishOTAProgress(int progress) {
  static char payload[32];
  snprintf(payload, sizeof(payload), "{\"progress\":%d}", progress);
  
  publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_OTA_PROGRESS), payload);
}

// Publish OTA complete message (to device-specific topic)
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

// ============================================================================
// OTA UPDATE EXECUTION
// ============================================================================

// Perform OTA update from custom server
bool performOTAUpdate(String version) {
  if (!WiFi.isConnected()) {
    DEBUG_PRINTLN("OTA: WiFi not connected");
    publishOTAComplete(false, "WiFi not connected");
    return false;
  }

  // Enter LED override mode for OTA progress indication
  beginLedOverride(LED_OTA_PROGRESS);
  
  String binaryUrl = String(UPDATE_SERVER_URL) + "/api/firmware/" + getPlatform();
  
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("OTA UPDATE STARTING");
  DEBUG_PRINTLN("===========================================");
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
  // ESP32 - Manual download with authentication
  HTTPClient http;
  bool isHttps = binaryUrl.startsWith("https://");
  
  // Track client type for proper cleanup
  WiFiClientSecure* secureClientPtr = nullptr;
  WiFiClient* plainClientPtr = nullptr;
  WiFiClient* clientPtr = nullptr;
  
  // Setup HTTP connection
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
  
  // Add authentication headers
  http.addHeader("X-API-Key", DOWNLOAD_API_KEY);
  http.addHeader("User-Agent", "SkiClockNeo-OTA");
  
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
  
  // Begin OTA update
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
  
  // Download in chunks with progress reporting
  uint8_t buff[512] = { 0 };
  size_t written = 0;
  int lastReportedProgress = 0;
  
  while (http.connected() && (written < contentLength)) {
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
      
      // Report progress every 10%
      int progress = (written * 100) / contentLength;
      if (progress >= lastReportedProgress + 10) {
        publishOTAProgress(progress);
        lastReportedProgress = progress;
      }
    }
    delay(1);
  }
  
  if (written == contentLength) {
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
  
  // Finalize update
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
  // ESP8266 - Manual download with authentication
  HTTPClient http;
  bool isHttps = binaryUrl.startsWith("https://");
  
  // Track client type for proper cleanup
  WiFiClientSecure* secureClientPtr = nullptr;
  WiFiClient* plainClientPtr = nullptr;
  WiFiClient* clientPtr = nullptr;
  
  // Setup HTTP connection
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
  
  // Add authentication headers
  http.addHeader("X-API-Key", DOWNLOAD_API_KEY);
  http.addHeader("User-Agent", "SkiClockNeo-OTA");
  
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
  
  // Begin OTA update
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
  
  // Download in chunks with progress reporting
  uint8_t buff[512] = { 0 };
  size_t written = 0;
  int lastReportedProgress = 0;
  
  while (http.connected() && (written < contentLength)) {
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
      
      // Report progress every 10%
      int progress = (written * 100) / contentLength;
      if (progress >= lastReportedProgress + 10) {
        publishOTAProgress(progress);
        lastReportedProgress = progress;
      }
    }
    delay(1);
  }
  
  if (written == contentLength) {
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
  
  // Finalize update
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

// ============================================================================
// PUBLIC API
// ============================================================================

// Trigger OTA update when MQTT version response indicates update available
void triggerOTAUpdate(String newVersion) {
  if (otaUpdateInProgress) {
    DEBUG_PRINTLN("OTA update already in progress");
    return;
  }
  
  if (!WiFi.isConnected()) {
    DEBUG_PRINTLN("OTA: WiFi not connected");
    return;
  }
  
  // Compare versions
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
