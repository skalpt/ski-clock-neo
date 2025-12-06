#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

// Include product config for local build credentials
#include "../../ski-clock-neo_config.h"

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
#include "../core/debug.h"
#include "../core/device_info.h"

// Update server configuration (injected at build time)
#ifndef UPDATE_SERVER_URL
  #error "UPDATE_SERVER_URL must be defined at compile time via -DUPDATE_SERVER_URL=\"...\""
#endif

#ifndef DOWNLOAD_API_KEY
  #error "DOWNLOAD_API_KEY must be defined at compile time via -DDOWNLOAD_API_KEY=\"...\""
#endif

// Firmware version (injected at build time)
#ifndef FIRMWARE_VERSION
  #error "FIRMWARE_VERSION must be defined at compile time via -DFIRMWARE_VERSION=\"...\""
#endif

// Global state
extern bool otaUpdateInProgress;

// Function declarations
void setupOTA();
void triggerOTAUpdate(String newVersion, bool isPinned = false);
bool performOTAUpdate(String version);
void publishOTAStart(String newVersion);
void publishOTAProgress(int progress);
void publishOTAComplete(bool success, String errorMessage = "");

#endif
