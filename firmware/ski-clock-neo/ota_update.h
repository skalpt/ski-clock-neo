#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#if defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <Update.h>
  #include <esp_https_ota.h>
  #include <esp_ota_ops.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266httpUpdate.h>
  #include <ESP8266HTTPClient.h>
#else
  #error "This code requires ESP32 or ESP8266"
#endif

#include <WiFiClientSecure.h>
#include "debug.h"

// Forward declare MQTT client and topic definitions (from mqtt_client.h)
// This prevents circular dependency
#if defined(ESP32) || defined(ESP8266)
  extern PubSubClient mqttClient;
  extern const char MQTT_TOPIC_OTA_START[];
  extern const char MQTT_TOPIC_OTA_PROGRESS[];
  extern const char MQTT_TOPIC_OTA_COMPLETE[];
  String getDeviceID();  // Forward declaration
  String getPlatform();  // Forward declaration (defined below)
#endif

// Update server configuration
// These MUST be injected at build time from GitHub Actions
// If these are not defined, the build will fail with an error
#ifndef UPDATE_SERVER_URL
  #error "UPDATE_SERVER_URL must be defined at compile time via -DUPDATE_SERVER_URL=\"...\""
#endif

#ifndef DOWNLOAD_API_KEY
  #error "DOWNLOAD_API_KEY must be defined at compile time via -DDOWNLOAD_API_KEY=\"...\""
#endif

// Firmware version - must match your GitHub release tag
// This MUST be injected at build time from GitHub Actions
#ifndef FIRMWARE_VERSION
  #error "FIRMWARE_VERSION must be defined at compile time via -DFIRMWARE_VERSION=\"...\""
#endif

// Global state
bool otaUpdateInProgress = false;

// Get the platform identifier for this device
// This must match the platform names used in GitHub Actions workflow
String getPlatform() {
#if defined(ESP32)
  // Detect ESP32 variant using Arduino board defines
  // These must be explicitly set via -DBOARD_VARIANT in GitHub Actions
  #if defined(BOARD_ESP32S3)
    return "esp32s3";
  #elif defined(BOARD_ESP32C3)
    return "esp32c3";
  #elif defined(BOARD_ESP32)
    return "esp32";
  #else
    // Fallback: try CONFIG_IDF_TARGET if available (ESP-IDF native defines)
    #if defined(CONFIG_IDF_TARGET_ESP32S3)
      return "esp32s3";
    #elif defined(CONFIG_IDF_TARGET_ESP32C3)
      return "esp32c3";
    #else
      return "esp32";  // Default to esp32 if variant unknown
    #endif
  #endif
#elif defined(ESP8266)
  // Detect ESP8266 board variant using Arduino board defines
  #if defined(BOARD_WEMOS_D1MINI)
    return "d1mini";
  #elif defined(BOARD_ESP01)
    return "esp01";
  #elif defined(BOARD_ESP12F)
    return "esp12f";
  #else
    // Fallback: try standard Arduino ESP8266 defines
    #if defined(ARDUINO_ESP8266_WEMOS_D1MINI)
      return "d1mini";
    #elif defined(ARDUINO_ESP8266_ESP01)
      return "esp01";
    #else
      return "esp12f";  // Default to esp12f for unknown ESP8266 variants
    #endif
  #endif
#else
  return "unknown";
#endif
}

// Parse version string to comparable integer
// Supports both formats:
// - Semantic: v1.2.3 -> 1002003 (major*1000000 + minor*1000 + patch)
// - Timestamp: 2025.11.19.1 -> year*100000000 + month*1000000 + day*10000 + build
long parseVersion(String version) {
  // Remove 'v' prefix if present
  if (version.startsWith("v") || version.startsWith("V")) {
    version = version.substring(1);
  }
  
  // Count dots to determine format
  int dotCount = 0;
  for (int i = 0; i < version.length(); i++) {
    if (version.charAt(i) == '.') dotCount++;
  }
  
  int firstDot = version.indexOf('.');
  int secondDot = version.indexOf('.', firstDot + 1);
  int thirdDot = version.indexOf('.', secondDot + 1);
  
  if (dotCount == 3 && firstDot > 0) {
    // Timestamp format: 2025.11.19.1
    int year = version.substring(0, firstDot).toInt();
    int month = version.substring(firstDot + 1, secondDot).toInt();
    int day = version.substring(secondDot + 1, thirdDot).toInt();
    int build = version.substring(thirdDot + 1).toInt();
    
    // Normalize year (2025 = base year)
    return (long)(year - 2025) * 100000000 + (long)month * 1000000 + (long)day * 10000 + (long)build;
  } else {
    // Semantic format: v1.2.3 (legacy support)
    int major = 0, minor = 0, patch = 0;
    
    if (firstDot > 0) {
      major = version.substring(0, firstDot).toInt();
      if (secondDot > firstDot) {
        minor = version.substring(firstDot + 1, secondDot).toInt();
        patch = version.substring(secondDot + 1).toInt();
      } else {
        minor = version.substring(firstDot + 1).toInt();
      }
    }
    
    return (long)major * 1000000 + (long)minor * 1000 + (long)patch;
  }
}

// Publish OTA start message to MQTT
void publishOTAStart(String newVersion) {
  if (!mqttClient.connected()) {
    return;
  }
  
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"platform\":\"%s\",\"old_version\":\"%s\",\"new_version\":\"%s\"}",
    getDeviceID().c_str(),
    getPlatform().c_str(),
    FIRMWARE_VERSION,
    newVersion.c_str()
  );
  
  mqttClient.publish(MQTT_TOPIC_OTA_START, payload);
  DEBUG_PRINT("OTA start published: ");
  DEBUG_PRINTLN(payload);
}

// Publish OTA progress message to MQTT
void publishOTAProgress(int progress) {
  if (!mqttClient.connected()) {
    return;
  }
  
  char payload[128];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"progress\":%d}",
    getDeviceID().c_str(),
    progress
  );
  
  mqttClient.publish(MQTT_TOPIC_OTA_PROGRESS, payload);
  DEBUG_PRINT("OTA progress: ");
  DEBUG_PRINT(progress);
  DEBUG_PRINTLN("%");
}

// Publish OTA complete message to MQTT
void publishOTAComplete(bool success, String errorMessage = "") {
  if (!mqttClient.connected()) {
    return;
  }
  
  char payload[384];
  if (success) {
    snprintf(payload, sizeof(payload),
      "{\"device_id\":\"%s\",\"status\":\"success\"}",
      getDeviceID().c_str()
    );
  } else {
    snprintf(payload, sizeof(payload),
      "{\"device_id\":\"%s\",\"status\":\"failed\",\"error\":\"%s\"}",
      getDeviceID().c_str(),
      errorMessage.c_str()
    );
  }
  
  mqttClient.publish(MQTT_TOPIC_OTA_COMPLETE, payload);
  DEBUG_PRINT("OTA complete published: ");
  DEBUG_PRINTLN(payload);
}

// Perform OTA update from custom server
bool performOTAUpdate(String version) {
  if (!WiFi.isConnected()) {
    DEBUG_PRINTLN("OTA: WiFi not connected");
    publishOTAComplete(false, "WiFi not connected");
    return false;
  }
  
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
  
  // Publish OTA start message
  publishOTAStart(version);
  
  otaUpdateInProgress = true;
  
#if defined(ESP32)
  // ESP32 - Manual download with authentication
  HTTPClient http;
  bool isHttps = binaryUrl.startsWith("https://");
  
  // Create client pointer that stays alive for entire download
  WiFiClient* clientPtr = nullptr;
  
  if (isHttps) {
    WiFiClientSecure* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();  // Skip cert validation for custom servers
    clientPtr = secureClient;
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
      publishOTAComplete(false, "Failed to begin HTTP connection");
      delete clientPtr;
      otaUpdateInProgress = false;
      return false;
    }
  } else {
    clientPtr = new WiFiClient();
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
      publishOTAComplete(false, "Failed to begin HTTP connection");
      delete clientPtr;
      otaUpdateInProgress = false;
      return false;
    }
  }
  
  http.addHeader("X-API-Key", DOWNLOAD_API_KEY);
  http.addHeader("User-Agent", "SkiClockNeo-OTA");
  
  DEBUG_PRINTLN("Starting ESP32 OTA download...");
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    DEBUG_PRINT("HTTP GET failed: ");
    DEBUG_PRINTLN(httpCode);
    publishOTAComplete(false, "HTTP GET failed");
    http.end();
    delete clientPtr;
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
    delete clientPtr;
    otaUpdateInProgress = false;
    return false;
  }
  
  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    DEBUG_PRINTLN("Not enough space for OTA");
    publishOTAComplete(false, "Not enough space for OTA");
    http.end();
    delete clientPtr;
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
        delete clientPtr;
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
    publishOTAProgress(100);  // Final progress update
  } else {
    DEBUG_PRINT("Written only ");
    DEBUG_PRINT(written);
    DEBUG_PRINT(" / ");
    DEBUG_PRINTLN(contentLength);
    publishOTAComplete(false, "Incomplete download");
    http.end();
    delete clientPtr;
    otaUpdateInProgress = false;
    return false;
  }
  
  if (Update.end()) {
    if (Update.isFinished()) {
      DEBUG_PRINTLN("OTA Update successful! Rebooting...");
      publishOTAComplete(true);
      http.end();
      delete clientPtr;
      delay(2000);
      ESP.restart();
      return true;
    } else {
      DEBUG_PRINTLN("Update not finished");
      publishOTAComplete(false, "Update not finished");
    }
  } else {
    DEBUG_PRINT("Update error: ");
    DEBUG_PRINTLN(Update.errorString());
    publishOTAComplete(false, Update.errorString());
  }
  
  http.end();
  delete clientPtr;
  otaUpdateInProgress = false;
  return false;
  
#elif defined(ESP8266)
  // ESP8266 - Manual download with authentication
  HTTPClient http;
  bool isHttps = binaryUrl.startsWith("https://");
  
  // Create client pointer that stays alive for entire download
  WiFiClient* clientPtr = nullptr;
  
  if (isHttps) {
    WiFiClientSecure* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();  // Skip cert validation for custom servers
    clientPtr = secureClient;
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
      publishOTAComplete(false, "Failed to begin HTTP connection");
      delete clientPtr;
      otaUpdateInProgress = false;
      return false;
    }
  } else {
    clientPtr = new WiFiClient();
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
      publishOTAComplete(false, "Failed to begin HTTP connection");
      delete clientPtr;
      otaUpdateInProgress = false;
      return false;
    }
  }
  
  http.addHeader("X-API-Key", DOWNLOAD_API_KEY);
  http.addHeader("User-Agent", "SkiClockNeo-OTA");
  
  DEBUG_PRINTLN("Starting ESP8266 OTA download...");
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    DEBUG_PRINT("HTTP GET failed: ");
    DEBUG_PRINTLN(httpCode);
    publishOTAComplete(false, "HTTP GET failed");
    http.end();
    delete clientPtr;
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
    delete clientPtr;
    otaUpdateInProgress = false;
    return false;
  }
  
  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    DEBUG_PRINTLN("Not enough space for OTA");
    publishOTAComplete(false, "Not enough space for OTA");
    http.end();
    delete clientPtr;
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
        delete clientPtr;
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
    publishOTAProgress(100);  // Final progress update
  } else {
    DEBUG_PRINT("Written only ");
    DEBUG_PRINT(written);
    DEBUG_PRINT(" / ");
    DEBUG_PRINTLN(contentLength);
    publishOTAComplete(false, "Incomplete download");
    http.end();
    delete clientPtr;
    otaUpdateInProgress = false;
    return false;
  }
  
  if (Update.end()) {
    if (Update.isFinished()) {
      DEBUG_PRINTLN("OTA Update successful! Rebooting...");
      publishOTAComplete(true);
      http.end();
      delete clientPtr;
      delay(2000);
      ESP.restart();
      return true;
    } else {
      DEBUG_PRINTLN("Update not finished");
      publishOTAComplete(false, "Update not finished");
    }
  } else {
    DEBUG_PRINT("Update error: ");
    DEBUG_PRINTLN(Update.getErrorString());
    publishOTAComplete(false, Update.getErrorString().c_str());
  }
  
  http.end();
  delete clientPtr;
  otaUpdateInProgress = false;
  return false;
#endif
  
  return false;
}

// Trigger OTA update when MQTT version response indicates update available
void triggerOTAUpdate(String newVersion) {
  // Skip if update already in progress
  if (otaUpdateInProgress) {
    DEBUG_PRINTLN("OTA update already in progress");
    return;
  }
  
  // Skip if WiFi not connected
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

// Initialize OTA updates
void setupOTA() {
  DEBUG_PRINTLN("OTA Update initialized (MQTT-triggered)");
  DEBUG_PRINT("Current firmware version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);
  DEBUG_PRINT("Update server: ");
  DEBUG_PRINTLN(UPDATE_SERVER_URL);
  
  #if defined(ESP32)
    // Print current partition info
    const esp_partition_t *running = esp_ota_get_running_partition();
    DEBUG_PRINT("Running partition: ");
    DEBUG_PRINTLN(running->label);
  #endif
}

#endif
