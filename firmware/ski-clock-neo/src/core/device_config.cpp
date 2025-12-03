// ============================================================================
// device_config.cpp - Persistent device configuration via MQTT
// ============================================================================
// This module handles device-specific configuration that can be modified
// remotely via MQTT and persists across reboots using NVS (ESP32) or
// EEPROM (ESP8266).
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "device_config.h"
#include "../../ski-clock-neo_config.h"
#include "debug.h"
#include "event_log.h"

#if defined(ESP32)
  #include <Preferences.h>
  static Preferences preferences;
#elif defined(ESP8266)
  #include <EEPROM.h>
  #define EEPROM_SIZE 64
  #define EEPROM_MAGIC 0xAB
  #define EEPROM_MAGIC_ADDR 0
  #define EEPROM_TEMP_OFFSET_ADDR 1
#endif

// ============================================================================
// STATE VARIABLES
// ============================================================================

static float temperatureOffset = TEMPERATURE_OFFSET;
static bool configInitialized = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

void initDeviceConfig() {
  DEBUG_PRINTLN("Initializing device configuration...");
  
#if defined(ESP32)
  preferences.begin("norrtek", false);
  
  if (preferences.isKey("temp_offset")) {
    temperatureOffset = preferences.getFloat("temp_offset", TEMPERATURE_OFFSET);
    DEBUG_PRINT("Loaded temperature offset from NVS: ");
    DEBUG_PRINTLN(temperatureOffset);
  } else {
    temperatureOffset = TEMPERATURE_OFFSET;
    DEBUG_PRINT("Using default temperature offset: ");
    DEBUG_PRINTLN(temperatureOffset);
  }
  
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  if (magic == EEPROM_MAGIC) {
    float stored;
    EEPROM.get(EEPROM_TEMP_OFFSET_ADDR, stored);
    if (!isnan(stored) && stored >= -20.0 && stored <= 20.0) {
      temperatureOffset = stored;
      DEBUG_PRINT("Loaded temperature offset from EEPROM: ");
      DEBUG_PRINTLN(temperatureOffset);
    } else {
      temperatureOffset = TEMPERATURE_OFFSET;
      DEBUG_PRINT("Invalid EEPROM value, using default: ");
      DEBUG_PRINTLN(temperatureOffset);
    }
  } else {
    temperatureOffset = TEMPERATURE_OFFSET;
    DEBUG_PRINT("No stored config, using default temperature offset: ");
    DEBUG_PRINTLN(temperatureOffset);
  }
#endif
  
  configInitialized = true;
  DEBUG_PRINTLN("Device configuration initialized");
}

// ============================================================================
// TEMPERATURE OFFSET
// ============================================================================

float getTemperatureOffset() {
  return temperatureOffset;
}

void setTemperatureOffset(float offset) {
  if (offset < -20.0 || offset > 20.0) {
    DEBUG_PRINT("Temperature offset out of range: ");
    DEBUG_PRINTLN(offset);
    return;
  }
  
  temperatureOffset = offset;
  DEBUG_PRINT("Temperature offset set to: ");
  DEBUG_PRINTLN(offset);
  
#if defined(ESP32)
  preferences.putFloat("temp_offset", offset);
  DEBUG_PRINTLN("Saved to NVS");
#elif defined(ESP8266)
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  EEPROM.put(EEPROM_TEMP_OFFSET_ADDR, offset);
  EEPROM.commit();
  DEBUG_PRINTLN("Saved to EEPROM");
#endif
  
  static char eventData[48];
  snprintf(eventData, sizeof(eventData), "{\"temp_offset\":%.1f}", offset);
  logEvent("config_updated", eventData);
}

// ============================================================================
// CONFIG MESSAGE HANDLER
// ============================================================================

static bool parseJsonFloat(const String& json, const char* key, float* value) {
  String searchKey = String("\"") + key + "\"";
  int keyIndex = json.indexOf(searchKey);
  if (keyIndex < 0) return false;
  
  int colonIndex = json.indexOf(":", keyIndex + searchKey.length());
  if (colonIndex < 0) return false;
  
  int startIndex = colonIndex + 1;
  while (startIndex < (int)json.length() && (json.charAt(startIndex) == ' ' || json.charAt(startIndex) == '\t' || json.charAt(startIndex) == '\n' || json.charAt(startIndex) == '\r')) {
    startIndex++;
  }
  
  if (startIndex >= (int)json.length()) return false;
  
  int endIndex = startIndex;
  bool hasDecimal = false;
  bool hasDigit = false;
  
  if (json.charAt(endIndex) == '-' || json.charAt(endIndex) == '+') {
    endIndex++;
  }
  
  while (endIndex < (int)json.length()) {
    char c = json.charAt(endIndex);
    if (c >= '0' && c <= '9') {
      hasDigit = true;
      endIndex++;
    } else if (c == '.' && !hasDecimal) {
      hasDecimal = true;
      endIndex++;
    } else {
      break;
    }
  }
  
  if (!hasDigit) return false;
  
  String valueStr = json.substring(startIndex, endIndex);
  *value = valueStr.toFloat();
  return true;
}

void handleConfigMessage(const String& message) {
  DEBUG_PRINT("Processing config message: ");
  DEBUG_PRINTLN(message);
  
  bool configUpdated = false;
  bool parseAttempted = false;
  
  float tempOffset;
  if (parseJsonFloat(message, "temp_offset", &tempOffset)) {
    parseAttempted = true;
    if (tempOffset >= -20.0 && tempOffset <= 20.0) {
      setTemperatureOffset(tempOffset);
      configUpdated = true;
      DEBUG_PRINT("Parsed temp_offset: ");
      DEBUG_PRINTLN(tempOffset);
    } else {
      DEBUG_PRINT("temp_offset out of range: ");
      DEBUG_PRINTLN(tempOffset);
      static char errData[64];
      snprintf(errData, sizeof(errData), "{\"error\":\"out_of_range\",\"value\":%.1f}", tempOffset);
      logEvent("config_error", errData);
    }
  }
  
  if (!configUpdated) {
    if (parseAttempted) {
      DEBUG_PRINTLN("Config value rejected (out of range)");
    } else if (message.indexOf("temp_offset") >= 0) {
      DEBUG_PRINTLN("Failed to parse temp_offset value");
      logEvent("config_error", "{\"error\":\"parse_failed\",\"key\":\"temp_offset\"}");
    } else {
      DEBUG_PRINTLN("No recognized config keys in message");
      logEvent("config_noop", nullptr);
    }
  }
}
