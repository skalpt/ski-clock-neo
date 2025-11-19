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

// Configuration - UPDATE THESE VALUES FOR YOUR GITHUB REPO
#ifndef GITHUB_REPO_OWNER
  #define GITHUB_REPO_OWNER "your-username"  // Change to your GitHub username
#endif

#ifndef GITHUB_REPO_NAME
  #define GITHUB_REPO_NAME "ski-clock-neo"  // Change to your repository name
#endif

// Firmware version - this should match your GitHub release tag
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION "v1.0.0"
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

// Get the appropriate binary filename for this platform
String getBinaryFilename() {
#if defined(ESP32)
  return "ski-clock-neo-esp32.bin";
#elif defined(ESP8266)
  return "ski-clock-neo-esp8266.bin";
#else
  return "unknown.bin";
#endif
}

// Parse version string (vX.Y.Z) to comparable integer
// v1.2.3 -> 1002003 (major*1000000 + minor*1000 + patch)
long parseVersion(String version) {
  // Remove 'v' prefix if present
  if (version.startsWith("v") || version.startsWith("V")) {
    version = version.substring(1);
  }
  
  int major = 0, minor = 0, patch = 0;
  int firstDot = version.indexOf('.');
  int secondDot = version.indexOf('.', firstDot + 1);
  
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

// Check GitHub API for latest release version
String getLatestVersion() {
  WiFiClientSecure client;
  HTTPClient https;
  
  client.setCACert(github_root_ca);
  
  String apiUrl = "https://api.github.com/repos/" + 
                  String(GITHUB_REPO_OWNER) + "/" + 
                  String(GITHUB_REPO_NAME) + "/releases/latest";
  
  Serial.print("Checking for updates at: ");
  Serial.println(apiUrl);
  
  if (https.begin(client, apiUrl)) {
    https.addHeader("Accept", "application/vnd.github+json");
    https.addHeader("User-Agent", "SkiClockNeo-OTA");
    
    int httpCode = https.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = https.getString();
      
      // Simple JSON parsing to extract "tag_name"
      int tagIndex = payload.indexOf("\"tag_name\"");
      if (tagIndex > 0) {
        int colonIndex = payload.indexOf(":", tagIndex);
        int quoteStart = payload.indexOf("\"", colonIndex + 1);
        int quoteEnd = payload.indexOf("\"", quoteStart + 1);
        
        if (quoteStart > 0 && quoteEnd > quoteStart) {
          String version = payload.substring(quoteStart + 1, quoteEnd);
          https.end();
          return version;
        }
      }
    } else {
      Serial.print("GitHub API error: ");
      Serial.println(httpCode);
    }
    
    https.end();
  }
  
  return "";  // Return empty string if failed
}

// Perform OTA update from GitHub release
bool performOTAUpdate(String version) {
  if (!WiFi.isConnected()) {
    Serial.println("OTA: WiFi not connected");
    return false;
  }
  
  String binaryUrl = "https://github.com/" + 
                     String(GITHUB_REPO_OWNER) + "/" + 
                     String(GITHUB_REPO_NAME) + "/releases/download/" + 
                     version + "/" + getBinaryFilename();
  
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
  // ESP32 - Use esp_https_ota
  WiFiClientSecure client;
  client.setCACert(github_root_ca);
  
  esp_http_client_config_t config = {};
  config.url = binaryUrl.c_str();
  config.cert_pem = github_root_ca;
  config.timeout_ms = 30000;
  config.keep_alive_enable = true;
  
  esp_https_ota_config_t ota_config = {};
  ota_config.http_config = &config;
  
  Serial.println("Starting ESP32 HTTPS OTA...");
  esp_err_t ret = esp_https_ota(&ota_config);
  
  if (ret == ESP_OK) {
    Serial.println("OTA Update successful! Rebooting...");
    delay(2000);
    ESP.restart();
    return true;
  } else {
    Serial.print("OTA Update failed: ");
    Serial.println(esp_err_to_name(ret));
    otaUpdateInProgress = false;
    return false;
  }
  
#elif defined(ESP8266)
  // ESP8266 - Use ESP8266HTTPUpdate
  WiFiClientSecure client;
  client.setCACert(github_root_ca);
  
  ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
  ESPhttpUpdate.rebootOnUpdate(false);
  
  Serial.println("Starting ESP8266 HTTPS OTA...");
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, binaryUrl);
  
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA Update failed (%d): %s\n", 
                    ESPhttpUpdate.getLastError(), 
                    ESPhttpUpdate.getLastErrorString().c_str());
      otaUpdateInProgress = false;
      return false;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("OTA: No updates available");
      otaUpdateInProgress = false;
      return false;
      
    case HTTP_UPDATE_OK:
      Serial.println("OTA Update successful! Rebooting...");
      delay(2000);
      ESP.restart();
      return true;
  }
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
  Serial.print("GitHub repo: ");
  Serial.print(GITHUB_REPO_OWNER);
  Serial.print("/");
  Serial.println(GITHUB_REPO_NAME);
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
