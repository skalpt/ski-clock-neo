#include "device_info.h"

String getDeviceID() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char deviceId[13];
  snprintf(deviceId, sizeof(deviceId), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(deviceId);
}

String getBoardType() {
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    return "ESP32-C3";
  #elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return "ESP32-S3";
  #elif defined(CONFIG_IDF_TARGET_ESP32S2)
    return "ESP32-S2";
  #elif defined(ESP32)
    return "ESP32";
  #elif defined(ESP8266)
    return "ESP8266";
  #else
    return "Unknown";
  #endif
}

String getPlatform() {
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    return "esp32c3";
  #elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return "esp32s3";
  #elif defined(CONFIG_IDF_TARGET_ESP32S2)
    return "esp32s2";
  #elif defined(ESP32)
    return "esp32";
  #elif defined(ESP8266)
    return "esp8266";
  #else
    return "unknown";
  #endif
}

long parseVersion(String version) {
  int major = 0, minor = 0, patch = 0;
  
  if (version.startsWith("v")) {
    version = version.substring(1);
  }
  
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
  
  return major * 10000L + minor * 100L + patch;
}
