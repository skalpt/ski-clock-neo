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
#include "certificates.h"

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

// OTA update check interval (default: 1 hour)
#ifndef OTA_CHECK_INTERVAL_MS
  #define OTA_CHECK_INTERVAL_MS 3600000  // 1 hour in milliseconds
#endif

// Global state
unsigned long lastOTACheckMs = 0;
unsigned long lastOTAAttemptMs = 0;
bool otaUpdateInProgress = false;

// Retry interval for failed checks (5 minutes)
#ifndef OTA_RETRY_INTERVAL_MS
  #define OTA_RETRY_INTERVAL_MS 300000  // 5 minutes in milliseconds
#endif

// Get the platform identifier for this device
String getPlatform() {
#if defined(ESP32)
  return "esp32";
#elif defined(ESP8266)
  return "esp8266";
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

// Check update server for latest version
String getLatestVersion() {
  HTTPClient http;
  
  String apiUrl = String(UPDATE_SERVER_URL) + "/api/version?platform=" + getPlatform();
  
  Serial.print("Checking for updates at: ");
  Serial.println(apiUrl);
  
  // Determine if we need HTTPS or HTTP
  bool isHttps = apiUrl.startsWith("https://");
  
  if (isHttps) {
    WiFiClientSecure client;
    client.setInsecure();  // For custom servers, cert validation is optional
    
    if (http.begin(client, apiUrl)) {
      http.addHeader("User-Agent", "SkiClockNeo-OTA");
      
      int httpCode = http.GET();
      
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        // Simple JSON parsing to extract "version"
        int versionIndex = payload.indexOf("\"version\"");
        if (versionIndex > 0) {
          int colonIndex = payload.indexOf(":", versionIndex);
          int quoteStart = payload.indexOf("\"", colonIndex + 1);
          int quoteEnd = payload.indexOf("\"", quoteStart + 1);
          
          if (quoteStart > 0 && quoteEnd > quoteStart) {
            String version = payload.substring(quoteStart + 1, quoteEnd);
            http.end();
            return version;
          }
        }
      } else {
        Serial.print("Update server error: ");
        Serial.println(httpCode);
      }
      
      http.end();
    }
  } else {
    // Plain HTTP
    WiFiClient client;
    
    if (http.begin(client, apiUrl)) {
      http.addHeader("User-Agent", "SkiClockNeo-OTA");
      
      int httpCode = http.GET();
      
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        // Simple JSON parsing to extract "version"
        int versionIndex = payload.indexOf("\"version\"");
        if (versionIndex > 0) {
          int colonIndex = payload.indexOf(":", versionIndex);
          int quoteStart = payload.indexOf("\"", colonIndex + 1);
          int quoteEnd = payload.indexOf("\"", quoteStart + 1);
          
          if (quoteStart > 0 && quoteEnd > quoteStart) {
            String version = payload.substring(quoteStart + 1, quoteEnd);
            http.end();
            return version;
          }
        }
      } else {
        Serial.print("Update server error: ");
        Serial.println(httpCode);
      }
      
      http.end();
    }
  }
  
  return "";  // Return empty string if failed
}

// Perform OTA update from custom server
bool performOTAUpdate(String version) {
  if (!WiFi.isConnected()) {
    Serial.println("OTA: WiFi not connected");
    return false;
  }
  
  String binaryUrl = String(UPDATE_SERVER_URL) + "/api/firmware/" + getPlatform();
  
  Serial.println("===========================================");
  Serial.println("OTA UPDATE STARTING");
  Serial.println("===========================================");
  Serial.print("Current version: ");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("New version: ");
  Serial.println(version);
  Serial.print("Download URL: ");
  Serial.println(binaryUrl);
  Serial.println("===========================================");
  
  otaUpdateInProgress = true;
  
#if defined(ESP32)
  // ESP32 - Manual download with authentication
  HTTPClient http;
  bool isHttps = binaryUrl.startsWith("https://");
  
  if (isHttps) {
    WiFiClientSecure client;
    client.setInsecure();  // Skip cert validation for custom servers
    
    if (!http.begin(client, binaryUrl)) {
      Serial.println("Failed to begin HTTP connection");
      otaUpdateInProgress = false;
      return false;
    }
  } else {
    WiFiClient client;
    
    if (!http.begin(client, binaryUrl)) {
      Serial.println("Failed to begin HTTP connection");
      otaUpdateInProgress = false;
      return false;
    }
  }
  
  http.addHeader("X-API-Key", DOWNLOAD_API_KEY);
  http.addHeader("User-Agent", "SkiClockNeo-OTA");
  
  Serial.println("Starting ESP32 OTA download...");
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("HTTP GET failed: ");
    Serial.println(httpCode);
    http.end();
    otaUpdateInProgress = false;
    return false;
  }
  
  int contentLength = http.getSize();
  Serial.print("Firmware size: ");
  Serial.println(contentLength);
  
  if (contentLength <= 0) {
    Serial.println("Invalid content length");
    http.end();
    otaUpdateInProgress = false;
    return false;
  }
  
  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    Serial.println("Not enough space for OTA");
    http.end();
    otaUpdateInProgress = false;
    return false;
  }
  
  WiFiClient * stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  
  if (written == contentLength) {
    Serial.println("Firmware written successfully");
  } else {
    Serial.print("Written only ");
    Serial.print(written);
    Serial.print(" / ");
    Serial.println(contentLength);
  }
  
  if (Update.end()) {
    if (Update.isFinished()) {
      Serial.println("OTA Update successful! Rebooting...");
      http.end();
      delay(2000);
      ESP.restart();
      return true;
    } else {
      Serial.println("Update not finished");
    }
  } else {
    Serial.print("Update error: ");
    Serial.println(Update.errorString());
  }
  
  http.end();
  otaUpdateInProgress = false;
  return false;
  
#elif defined(ESP8266)
  // ESP8266 - Manual download with authentication
  HTTPClient http;
  bool isHttps = binaryUrl.startsWith("https://");
  
  if (isHttps) {
    WiFiClientSecure client;
    client.setInsecure();  // Skip cert validation for custom servers
    
    if (!http.begin(client, binaryUrl)) {
      Serial.println("Failed to begin HTTP connection");
      otaUpdateInProgress = false;
      return false;
    }
  } else {
    WiFiClient client;
    
    if (!http.begin(client, binaryUrl)) {
      Serial.println("Failed to begin HTTP connection");
      otaUpdateInProgress = false;
      return false;
    }
  }
  
  http.addHeader("X-API-Key", DOWNLOAD_API_KEY);
  http.addHeader("User-Agent", "SkiClockNeo-OTA");
  
  Serial.println("Starting ESP8266 OTA download...");
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("HTTP GET failed: ");
    Serial.println(httpCode);
    http.end();
    otaUpdateInProgress = false;
    return false;
  }
  
  int contentLength = http.getSize();
  Serial.print("Firmware size: ");
  Serial.println(contentLength);
  
  if (contentLength <= 0) {
    Serial.println("Invalid content length");
    http.end();
    otaUpdateInProgress = false;
    return false;
  }
  
  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    Serial.println("Not enough space for OTA");
    http.end();
    otaUpdateInProgress = false;
    return false;
  }
  
  WiFiClient * stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  
  if (written == contentLength) {
    Serial.println("Firmware written successfully");
  } else {
    Serial.print("Written only ");
    Serial.print(written);
    Serial.print(" / ");
    Serial.println(contentLength);
  }
  
  if (Update.end()) {
    if (Update.isFinished()) {
      Serial.println("OTA Update successful! Rebooting...");
      http.end();
      delay(2000);
      ESP.restart();
      return true;
    } else {
      Serial.println("Update not finished");
    }
  } else {
    Serial.print("Update error: ");
    Serial.println(Update.getErrorString());
  }
  
  http.end();
  otaUpdateInProgress = false;
  return false;
#endif
  
  return false;
}

// Check for updates and install if available
void checkForOTAUpdate(bool force = false) {
  unsigned long nowMs = millis();
  
  // Skip if update already in progress
  if (otaUpdateInProgress) {
    return;
  }
  
  // Determine appropriate interval based on last attempt success
  // If last check was successful, use full interval
  // If last check failed, use shorter retry interval
  unsigned long timeSinceSuccess = nowMs - lastOTACheckMs;
  unsigned long timeSinceAttempt = nowMs - lastOTAAttemptMs;
  
  if (!force) {
    // If we've had a successful check recently, wait for full interval
    if (lastOTACheckMs > 0 && timeSinceSuccess < OTA_CHECK_INTERVAL_MS) {
      return;
    }
    // If we've attempted recently (but failed), use shorter retry interval
    if (lastOTAAttemptMs > 0 && timeSinceAttempt < OTA_RETRY_INTERVAL_MS) {
      return;
    }
  }
  
  // Record this attempt
  lastOTAAttemptMs = nowMs;
  
  // Skip if WiFi not connected
  if (!WiFi.isConnected()) {
    Serial.println("OTA: Skipping check - WiFi not connected");
    return;
  }
  
  Serial.println("Checking for firmware updates...");
  
  String latestVersion = getLatestVersion();
  
  if (latestVersion.length() == 0) {
    Serial.println("OTA: Could not retrieve latest version - will retry in 5 minutes");
    return;
  }
  
  Serial.print("Latest version: ");
  Serial.println(latestVersion);
  
  // Compare versions
  long currentVer = parseVersion(FIRMWARE_VERSION);
  long latestVer = parseVersion(latestVersion);
  
  Serial.print("Current version code: ");
  Serial.println(currentVer);
  Serial.print("Latest version code: ");
  Serial.println(latestVer);
  
  if (latestVer > currentVer) {
    Serial.println("New version available! Starting OTA update...");
    bool updateSuccess = performOTAUpdate(latestVersion);
    
    // Only update success timestamp if download/install succeeded
    // Failed downloads will retry in 5 minutes via lastOTAAttemptMs
    if (updateSuccess) {
      lastOTACheckMs = nowMs;
    }
  } else {
    Serial.println("Firmware is up to date");
    // Update success timestamp - no new version available
    lastOTACheckMs = nowMs;
  }
}

// Initialize OTA updates
void setupOTA() {
  Serial.println("OTA Update initialized");
  Serial.print("Current firmware version: ");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("Update server: ");
  Serial.println(UPDATE_SERVER_URL);
  Serial.print("Check interval: ");
  Serial.print(OTA_CHECK_INTERVAL_MS / 1000);
  Serial.println(" seconds");
  
#if defined(ESP32)
  // Print current partition info
  const esp_partition_t *running = esp_ota_get_running_partition();
  Serial.print("Running partition: ");
  Serial.println(running->label);
#endif
}

// Call this in loop() to handle periodic update checks
void updateOTA() {
  checkForOTAUpdate(false);
}

// Force an immediate update check
void forceOTACheck() {
  Serial.println("Forcing OTA update check...");
  checkForOTAUpdate(true);
}

#endif
