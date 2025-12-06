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
#include "../connectivity/mqtt_client.h"

#if defined(ESP32)
  #include <Preferences.h>
  static Preferences preferences;
#elif defined(ESP8266)
  #include <EEPROM.h>
  #define EEPROM_SIZE 64
  #define EEPROM_MAGIC 0xAB
  #define EEPROM_MAGIC_ADDR 0
  #define EEPROM_TEMP_OFFSET_ADDR 1
  #define EEPROM_ENV_SCOPE_ADDR 5  // After float (4 bytes) + magic (1 byte)
  #define EEPROM_ENV_SCOPE_LEN 8   // "dev" or "prod" + null terminator
#endif

// ============================================================================
// STATE VARIABLES
// ============================================================================

static float temperatureOffset = TEMPERATURE_OFFSET;
static char environmentScope[8] = "";  // Initialized from ENV_SCOPE in initDeviceConfig()
static bool configInitialized = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

void initDeviceConfig() {
  DEBUG_PRINTLN("Initializing device configuration...");
  
  // Set compile-time default for environment scope
  strncpy(environmentScope, ENV_SCOPE, sizeof(environmentScope) - 1);
  environmentScope[sizeof(environmentScope) - 1] = '\0';
  
#if defined(ESP32)
  preferences.begin("norrtek", false);
  
  // Load temperature offset
  if (preferences.isKey("temp_offset")) {
    temperatureOffset = preferences.getFloat("temp_offset", TEMPERATURE_OFFSET);
    DEBUG_PRINT("Loaded temperature offset from NVS: ");
    DEBUG_PRINTLN(temperatureOffset);
  } else {
    temperatureOffset = TEMPERATURE_OFFSET;
    DEBUG_PRINT("Using default temperature offset: ");
    DEBUG_PRINTLN(temperatureOffset);
  }
  
  // Load environment scope (overrides compile-time default if set)
  if (preferences.isKey("env_scope")) {
    String storedEnv = preferences.getString("env_scope", ENV_SCOPE);
    if (storedEnv == "dev" || storedEnv == "prod") {
      strncpy(environmentScope, storedEnv.c_str(), sizeof(environmentScope) - 1);
      environmentScope[sizeof(environmentScope) - 1] = '\0';
      DEBUG_PRINT("Loaded environment scope from NVS: ");
      DEBUG_PRINTLN(environmentScope);
    }
  } else {
    DEBUG_PRINT("Using compile-time environment scope: ");
    DEBUG_PRINTLN(environmentScope);
  }
  
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  if (magic == EEPROM_MAGIC) {
    // Load temperature offset
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
    
    // Load environment scope
    char storedEnv[EEPROM_ENV_SCOPE_LEN];
    for (int i = 0; i < EEPROM_ENV_SCOPE_LEN; i++) {
      storedEnv[i] = EEPROM.read(EEPROM_ENV_SCOPE_ADDR + i);
    }
    storedEnv[EEPROM_ENV_SCOPE_LEN - 1] = '\0';
    if (strcmp(storedEnv, "dev") == 0 || strcmp(storedEnv, "prod") == 0) {
      strncpy(environmentScope, storedEnv, sizeof(environmentScope) - 1);
      environmentScope[sizeof(environmentScope) - 1] = '\0';
      DEBUG_PRINT("Loaded environment scope from EEPROM: ");
      DEBUG_PRINTLN(environmentScope);
    }
  } else {
    temperatureOffset = TEMPERATURE_OFFSET;
    DEBUG_PRINT("No stored config, using defaults");
    DEBUG_PRINTLN("");
  }
#endif
  
  configInitialized = true;
  DEBUG_PRINT("Device configuration initialized (env: ");
  DEBUG_PRINT(environmentScope);
  DEBUG_PRINTLN(")");
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
  
  // Publish updated device info to confirm config change
  publishDeviceInfo();
}

// ============================================================================
// ENVIRONMENT SCOPE
// ============================================================================

const char* getEnvironmentScope() {
  return environmentScope;
}

void setEnvironmentScope(const char* scope) {
  // Validate scope (must be "dev" or "prod")
  if (strcmp(scope, "dev") != 0 && strcmp(scope, "prod") != 0) {
    DEBUG_PRINT("Invalid environment scope: ");
    DEBUG_PRINTLN(scope);
    static char errData[64];
    snprintf(errData, sizeof(errData), "{\"error\":\"invalid_scope\",\"value\":\"%s\"}", scope);
    logEvent("config_error", errData);
    return;
  }
  
  // Check if scope is actually changing
  if (strcmp(environmentScope, scope) == 0) {
    DEBUG_PRINT("Environment scope unchanged: ");
    DEBUG_PRINTLN(scope);
    logEvent("config_noop", "{\"key\":\"environment\"}");
    return;
  }
  
  strncpy(environmentScope, scope, sizeof(environmentScope) - 1);
  environmentScope[sizeof(environmentScope) - 1] = '\0';
  DEBUG_PRINT("Environment scope set to: ");
  DEBUG_PRINTLN(scope);
  
#if defined(ESP32)
  preferences.putString("env_scope", scope);
  DEBUG_PRINTLN("Saved to NVS");
#elif defined(ESP8266)
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  for (int i = 0; i < EEPROM_ENV_SCOPE_LEN; i++) {
    if (i < (int)strlen(scope)) {
      EEPROM.write(EEPROM_ENV_SCOPE_ADDR + i, scope[i]);
    } else {
      EEPROM.write(EEPROM_ENV_SCOPE_ADDR + i, '\0');
    }
  }
  EEPROM.commit();
  DEBUG_PRINTLN("Saved to EEPROM");
#endif
  
  static char eventData[48];
  snprintf(eventData, sizeof(eventData), "{\"environment\":\"%s\"}", scope);
  logEvent("config_updated", eventData);
  
  // Publish updated device info before reconnecting
  publishDeviceInfo();
  
  // Trigger MQTT reconnect to switch topics
  DEBUG_PRINTLN("Environment changed - triggering MQTT reconnect...");
  disconnectMQTT();
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

static bool parseJsonString(const String& json, const char* key, String* value) {
  String searchKey = String("\"") + key + "\"";
  int keyIndex = json.indexOf(searchKey);
  if (keyIndex < 0) return false;
  
  int colonIndex = json.indexOf(":", keyIndex + searchKey.length());
  if (colonIndex < 0) return false;
  
  int quoteStart = json.indexOf("\"", colonIndex + 1);
  if (quoteStart < 0) return false;
  
  int quoteEnd = json.indexOf("\"", quoteStart + 1);
  if (quoteEnd < 0) return false;
  
  *value = json.substring(quoteStart + 1, quoteEnd);
  return true;
}

void handleConfigMessage(const String& message) {
  DEBUG_PRINT("Processing config message: ");
  DEBUG_PRINTLN(message);
  
  bool configUpdated = false;
  bool parseAttempted = false;
  
  // Parse temperature offset
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
  
  // Parse environment scope
  String envScope;
  if (parseJsonString(message, "environment", &envScope)) {
    parseAttempted = true;
    setEnvironmentScope(envScope.c_str());
    configUpdated = true;
    DEBUG_PRINT("Parsed environment: ");
    DEBUG_PRINTLN(envScope);
  }
  
  if (!configUpdated) {
    if (parseAttempted) {
      DEBUG_PRINTLN("Config value rejected");
    } else if (message.indexOf("temp_offset") >= 0) {
      DEBUG_PRINTLN("Failed to parse temp_offset value");
      logEvent("config_error", "{\"error\":\"parse_failed\",\"key\":\"temp_offset\"}");
    } else if (message.indexOf("environment") >= 0) {
      DEBUG_PRINTLN("Failed to parse environment value");
      logEvent("config_error", "{\"error\":\"parse_failed\",\"key\":\"environment\"}");
    } else {
      DEBUG_PRINTLN("No recognized config keys in message");
      logEvent("config_noop", nullptr);
    }
  }
}
