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
#include <Ticker.h>
#include "debug.h"

// OTA Ticker (software-driven for non-blocking operation)
Ticker otaTicker;

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

// Check update server for latest version (non-blocking with chunked reads)
String getLatestVersion() {
  HTTPClient http;
  
  String apiUrl = String(UPDATE_SERVER_URL) + "/api/version?platform=" + getPlatform();
  
  DEBUG_PRINT("Checking for updates at: ");
  DEBUG_PRINTLN(apiUrl);
  
  // Determine if we need HTTPS or HTTP
  bool isHttps = apiUrl.startsWith("https://");
  
  WiFiClient* clientPtr = nullptr;
  
  if (isHttps) {
    WiFiClientSecure* secureClient = new WiFiClientSecure();
    secureClient->setInsecure();  // For custom servers, cert validation is optional
    clientPtr = secureClient;
  } else {
    clientPtr = new WiFiClient();
  }
  
  if (!http.begin(*clientPtr, apiUrl)) {
    DEBUG_PRINTLN("Failed to begin HTTP connection");
    delete clientPtr;
    return "";
  }
  
  http.addHeader("User-Agent", "SkiClockNeo-OTA");
  
  int httpCode = http.GET();
  
  if (httpCode != HTTP_CODE_OK) {
    DEBUG_PRINT("Update server error: ");
    DEBUG_PRINTLN(httpCode);
    http.end();
    delete clientPtr;
    return "";
  }
  
  // Get content length
  int contentLength = http.getSize();
  DEBUG_PRINT("Response size: ");
  DEBUG_PRINTLN(contentLength);
  
  // Read response in chunks with yield() to keep system responsive
  String payload = "";
  WiFiClient* stream = http.getStreamPtr();
  
  unsigned long startTime = millis();
  const unsigned long TIMEOUT_MS = 10000;  // 10 second timeout
  const int CHUNK_SIZE = 32;  // Read 32 bytes at a time
  const int MAX_RESPONSE_SIZE = 1024;  // Max 1KB for version response
  
  while (http.connected() && (contentLength > 0 || contentLength == -1)) {
    // Check timeout
    if (millis() - startTime > TIMEOUT_MS) {
      DEBUG_PRINTLN("OTA version check timeout");
      break;
    }
    
    // Safety check: prevent unbounded memory growth
    if (payload.length() >= MAX_RESPONSE_SIZE) {
      DEBUG_PRINTLN("OTA version response too large, truncating");
      break;
    }
    
    // Get available data size
    size_t available = stream->available();
    
    if (available) {
      // Read up to CHUNK_SIZE bytes
      int readSize = (available > CHUNK_SIZE) ? CHUNK_SIZE : available;
      char buffer[CHUNK_SIZE + 1];
      int bytesRead = stream->readBytes(buffer, readSize);
      
      if (bytesRead > 0) {
        buffer[bytesRead] = '\0';
        payload += String(buffer);
        
        if (contentLength > 0) {
          contentLength -= bytesRead;
        }
        
        // Yield to system to allow tickers/WiFi to run
        yield();
      }
    } else {
      // No data available, yield and wait a bit
      yield();
      delay(1);
    }
  }
  
  DEBUG_PRINT("Received payload: ");
  DEBUG_PRINTLN(payload);
  
  http.end();
  delete clientPtr;
  
  // Simple JSON parsing to extract "version"
  int versionIndex = payload.indexOf("\"version\"");
  if (versionIndex > 0) {
    int colonIndex = payload.indexOf(":", versionIndex);
    int quoteStart = payload.indexOf("\"", colonIndex + 1);
    int quoteEnd = payload.indexOf("\"", quoteStart + 1);
    
    if (quoteStart > 0 && quoteEnd > quoteStart) {
      String version = payload.substring(quoteStart + 1, quoteEnd);
      return version;
    }
  }
  
  return "";  // Return empty string if failed
}

// Perform OTA update from custom server
bool performOTAUpdate(String version) {
  if (!WiFi.isConnected()) {
    DEBUG_PRINTLN("OTA: WiFi not connected");
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
      delete clientPtr;
      otaUpdateInProgress = false;
      return false;
    }
  } else {
    clientPtr = new WiFiClient();
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
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
    http.end();
    delete clientPtr;
    otaUpdateInProgress = false;
    return false;
  }
  
  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    DEBUG_PRINTLN("Not enough space for OTA");
    http.end();
    delete clientPtr;
    otaUpdateInProgress = false;
    return false;
  }
  
  WiFiClient * stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  
  if (written == contentLength) {
    DEBUG_PRINTLN("Firmware written successfully");
  } else {
    DEBUG_PRINT("Written only ");
    DEBUG_PRINT(written);
    DEBUG_PRINT(" / ");
    DEBUG_PRINTLN(contentLength);
  }
  
  if (Update.end()) {
    if (Update.isFinished()) {
      DEBUG_PRINTLN("OTA Update successful! Rebooting...");
      http.end();
      delete clientPtr;
      delay(2000);
      ESP.restart();
      return true;
    } else {
      DEBUG_PRINTLN("Update not finished");
    }
  } else {
    DEBUG_PRINT("Update error: ");
    DEBUG_PRINTLN(Update.errorString());
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
      delete clientPtr;
      otaUpdateInProgress = false;
      return false;
    }
  } else {
    clientPtr = new WiFiClient();
    
    if (!http.begin(*clientPtr, binaryUrl)) {
      DEBUG_PRINTLN("Failed to begin HTTP connection");
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
    http.end();
    delete clientPtr;
    otaUpdateInProgress = false;
    return false;
  }
  
  bool canBegin = Update.begin(contentLength);
  if (!canBegin) {
    DEBUG_PRINTLN("Not enough space for OTA");
    http.end();
    delete clientPtr;
    otaUpdateInProgress = false;
    return false;
  }
  
  WiFiClient * stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  
  if (written == contentLength) {
    DEBUG_PRINTLN("Firmware written successfully");
  } else {
    DEBUG_PRINT("Written only ");
    DEBUG_PRINT(written);
    DEBUG_PRINT(" / ");
    DEBUG_PRINTLN(contentLength);
  }
  
  if (Update.end()) {
    if (Update.isFinished()) {
      DEBUG_PRINTLN("OTA Update successful! Rebooting...");
      http.end();
      delete clientPtr;
      delay(2000);
      ESP.restart();
      return true;
    } else {
      DEBUG_PRINTLN("Update not finished");
    }
  } else {
    DEBUG_PRINT("Update error: ");
    DEBUG_PRINTLN(Update.getErrorString());
  }
  
  http.end();
  delete clientPtr;
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
    DEBUG_PRINTLN("OTA: Skipping check - WiFi not connected");
    return;
  }
  
  DEBUG_PRINTLN("Checking for firmware updates...");
  
  String latestVersion = getLatestVersion();
  
  if (latestVersion.length() == 0) {
    DEBUG_PRINTLN("OTA: Could not retrieve latest version - will retry in 5 minutes");
    return;
  }
  
  DEBUG_PRINT("Latest version: ");
  DEBUG_PRINTLN(latestVersion);
  
  // Compare versions
  long currentVer = parseVersion(FIRMWARE_VERSION);
  long latestVer = parseVersion(latestVersion);
  
  DEBUG_PRINT("Current version code: ");
  DEBUG_PRINTLN(currentVer);
  DEBUG_PRINT("Latest version code: ");
  DEBUG_PRINTLN(latestVer);
  
  if (latestVer > currentVer) {
    DEBUG_PRINTLN("New version available! Starting OTA update...");
    bool updateSuccess = performOTAUpdate(latestVersion);
    
    // Only update success timestamp if download/install succeeded
    // Failed downloads will retry in 5 minutes via lastOTAAttemptMs
    if (updateSuccess) {
      lastOTACheckMs = nowMs;
    }
  } else {
    DEBUG_PRINTLN("Firmware is up to date");
    // Update success timestamp - no new version available
    lastOTACheckMs = nowMs;
  }
}

// Initialize OTA updates
void setupOTA() {
  DEBUG_PRINTLN("OTA Update initialized");
  DEBUG_PRINT("Current firmware version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);
  DEBUG_PRINT("Update server: ");
  DEBUG_PRINTLN(UPDATE_SERVER_URL);
  DEBUG_PRINT("Check interval: ");
  DEBUG_PRINT(OTA_CHECK_INTERVAL_MS / 1000);
  DEBUG_PRINTLN(" seconds");
  
#if defined(ESP32)
  // Print current partition info
  const esp_partition_t *running = esp_ota_get_running_partition();
  DEBUG_PRINT("Running partition: ");
  DEBUG_PRINTLN(running->label);
#endif
}

// Call this in loop() to handle periodic update checks
void updateOTA() {
  checkForOTAUpdate(false);
}

// Force an immediate update check
void forceOTACheck() {
  DEBUG_PRINTLN("Forcing OTA update check...");
  checkForOTAUpdate(true);
  
  // Schedule next check in 1 hour (software ticker)
  otaTicker.once(3600, forceOTACheck);
  DEBUG_PRINTLN("Next OTA check scheduled in 1 hour");
}

// Initialize OTA system with initial check delay
void setupOTA(int initialDelaySeconds) {
  DEBUG_PRINT("OTA system initialized. First check in ");
  DEBUG_PRINT(initialDelaySeconds);
  DEBUG_PRINTLN(" seconds");
  
  // Schedule initial OTA check using one-shot ticker
  otaTicker.once(initialDelaySeconds, forceOTACheck);
}

#endif
