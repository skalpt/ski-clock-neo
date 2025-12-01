// ============================================================================
// device_info.cpp - Device identification and version parsing
// ============================================================================
// This library provides device identification functions:
// - Unique device ID from ESP chip ID
// - Board type detection for display purposes
// - Platform identifier for firmware downloads
// - Version string parsing for OTA comparisons
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "device_info.h"  // This file's header

// ============================================================================
// DEVICE IDENTIFICATION
// ============================================================================

// Get unique device ID (hex string derived from chip ID)
String getDeviceID() {
  #if defined(ESP32)
    uint64_t chipid = ESP.getEfuseMac();
    return String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
  #elif defined(ESP8266)
    return String(ESP.getChipId(), HEX);
  #endif
}

// Get board type as human-readable string (for display/logging)
String getBoardType() {
  #if defined(BOARD_ESP32)
    return "ESP32";
  #elif defined(BOARD_ESP32C3)
    return "ESP32-C3";
  #elif defined(BOARD_ESP32S3)
    return "ESP32-S3";
  #elif defined(BOARD_ESP12F)
    return "ESP-12F";
  #elif defined(BOARD_ESP01)
    return "ESP-01";
  #elif defined(BOARD_WEMOS_D1MINI)
    return "Wemos D1 Mini";
  #else
    return "Unknown";
  #endif
}

// Get platform identifier for firmware downloads (matches server naming)
String getPlatform() {
#if defined(ESP32)
  // Detect ESP32 variant using Arduino board defines
  #if defined(BOARD_ESP32S3)
    return "esp32s3";
  #elif defined(BOARD_ESP32C3)
    return "esp32c3";
  #elif defined(BOARD_ESP32)
    return "esp32";
  #else
    // Fallback: try CONFIG_IDF_TARGET if available
    #if defined(CONFIG_IDF_TARGET_ESP32S3)
      return "esp32s3";
    #elif defined(CONFIG_IDF_TARGET_ESP32C3)
      return "esp32c3";
    #else
      return "esp32";
    #endif
  #endif
#elif defined(ESP8266)
  // Detect ESP8266 board variant
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
      return "esp12f";
    #endif
  #endif
#else
  return "unknown";
#endif
}

// ============================================================================
// VERSION PARSING
// ============================================================================

// Parse version string to comparable integer
// Supports both timestamp format (2025.11.19.1) and semantic format (v1.2.3)
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
