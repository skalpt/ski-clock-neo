// ============================================================================
// mqtt_client.cpp - MQTT communication with HiveMQ Cloud
// ============================================================================
// This library manages MQTT connectivity and messaging:
// - TLS-encrypted connection to HiveMQ Cloud
// - Heartbeat publishing every 60 seconds
// - Version checking and OTA update triggering
// - Display snapshot publishing (hourly + on-demand)
// - Remote command handling (restart, rollback, snapshot)
// - Event queue flushing on connect
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "mqtt_client.h"
#include "../../ski-clock-neo_config.h"
#include "../core/led_indicator.h"
#include "../core/device_config.h"
#include "../data/data_time.h"
#include "ota_update.h"
#include "../display/display_core.h"
#include "../core/event_log.h"

// ============================================================================
// CONSTANTS
// ============================================================================

const uint16_t MQTT_PORT = 8883;

const char MQTT_TOPIC_HEARTBEAT[] = "heartbeat";
const char MQTT_TOPIC_INFO[] = "info";
const char MQTT_TOPIC_VERSION_RESPONSE[] = "version/response";
const char MQTT_TOPIC_COMMAND[] = "command";
const char MQTT_TOPIC_CONFIG[] = "config";
const char MQTT_TOPIC_OTA_START[] = "ota/start";
const char MQTT_TOPIC_OTA_PROGRESS[] = "ota/progress";
const char MQTT_TOPIC_OTA_COMPLETE[] = "ota/complete";
const char MQTT_TOPIC_DISPLAY_SNAPSHOT[] = "display/snapshot";
const char MQTT_TOPIC_EVENTS[] = "event";

const unsigned long HEARTBEAT_INTERVAL = 60000;
const unsigned long DISPLAY_SNAPSHOT_INTERVAL = 3600000;

// ============================================================================
// STATE VARIABLES
// ============================================================================

WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);
Ticker heartbeatTicker;
Ticker displaySnapshotTicker;
bool mqttIsConnected = false;
bool mqttInitialized = false;

static const char BASE64_CHARS[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void mqttCallback(char* topic, byte* payload, unsigned int length);
String base64Encode(const uint8_t* data, uint16_t length);

// ============================================================================
// MQTT MESSAGE CALLBACK
// ============================================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  DEBUG_PRINT("MQTT message received on topic: ");
  DEBUG_PRINTLN(topicStr);
  DEBUG_PRINT("Message: ");
  DEBUG_PRINTLN(message);
  
  String versionResponseTopic = buildDeviceTopic(MQTT_TOPIC_VERSION_RESPONSE);
  if (topicStr == versionResponseTopic) {
    DEBUG_PRINTLN("Version response received!");
    
    if (message.indexOf("\"update_available\": true") > 0 || message.indexOf("\"update_available\":true") > 0) {
      int versionIndex = message.indexOf("\"latest_version\"");
      if (versionIndex > 0) {
        int colonIndex = message.indexOf(":", versionIndex);
        int quoteStart = message.indexOf("\"", colonIndex + 1);
        int quoteEnd = message.indexOf("\"", quoteStart + 1);
        
        if (quoteStart > 0 && quoteEnd > quoteStart) {
          String latestVersion = message.substring(quoteStart + 1, quoteEnd);
          DEBUG_PRINT("New version available: ");
          DEBUG_PRINTLN(latestVersion);
          
          bool isPinned = false;
          int pinnedIndex = message.indexOf("\"pinned\"");
          if (pinnedIndex > 0) {
            int pinnedColonIndex = message.indexOf(":", pinnedIndex);
            if (pinnedColonIndex > pinnedIndex) {
              String valueSection = message.substring(pinnedColonIndex + 1, pinnedColonIndex + 10);
              valueSection.toLowerCase();
              valueSection.trim();
              isPinned = valueSection.startsWith("true");
            }
          }
          if (isPinned) {
            DEBUG_PRINTLN("Device is pinned to this version");
          }
          
          triggerOTAUpdate(latestVersion, isPinned);
        }
      }
    } else {
      DEBUG_PRINTLN("Firmware is up to date");
    }
  }
  
  String commandTopic = buildDeviceTopic(MQTT_TOPIC_COMMAND);
  if (topicStr == commandTopic) {
    DEBUG_PRINTLN("Command received!");
    
    if (message.indexOf("rollback") >= 0) {
      DEBUG_PRINTLN("Executing rollback command");
      handleRollbackCommand(message);
    } else if (message.indexOf("restart") >= 0) {
      DEBUG_PRINTLN("Executing restart command");
      handleRestartCommand();
    } else if (message.indexOf("snapshot") >= 0) {
      DEBUG_PRINTLN("Executing snapshot command");
      publishDisplaySnapshot();
    } else if (message.indexOf("info") >= 0) {
      DEBUG_PRINTLN("Executing info command");
      publishDeviceInfo();
    } else {
      DEBUG_PRINTLN("Unknown command type");
    }
  }
  
  String configTopic = buildDeviceTopic(MQTT_TOPIC_CONFIG);
  if (topicStr == configTopic) {
    DEBUG_PRINTLN("Config message received!");
    handleConfigMessage(message);
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initMQTT() {
  DEBUG_PRINTLN("Initializing MQTT client...");
  
  wifiSecureClient.setInsecure();
  
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);
  
  DEBUG_PRINT("MQTT broker: ");
  DEBUG_PRINT(MQTT_HOST);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(MQTT_PORT);
  DEBUG_PRINTLN("TLS encryption enabled (no cert validation)");

  mqttInitialized = true;

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool connectMQTT() {
  if (!mqttInitialized) {
    DEBUG_PRINTLN("MQTT not initialized yet, skipping connection attempt");
    return false;
  }
  
  if (mqttClient.connected()) {
    setConnectivityState(true, true);
    return true;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("WiFi not connected, skipping MQTT connection");
    return false;
  }
  
  DEBUG_PRINTLN("Connecting to MQTT broker...");
  
  String clientId = "NorrtekDevice-" + getDeviceID();
  
  if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD))
  {
    DEBUG_PRINTLN("MQTT connected successfully");
    setConnectivityState(true, true);
    mqttIsConnected = true;

    logEvent("mqtt_connect");
    setEventLogReady(true);
    flushEventQueue();

    String versionResponseTopic = buildDeviceTopic(MQTT_TOPIC_VERSION_RESPONSE);
    if (mqttClient.subscribe(versionResponseTopic.c_str())) {
      DEBUG_PRINT("Subscribed: ");
      DEBUG_PRINTLN(versionResponseTopic);
    }
    
    String commandTopic = buildDeviceTopic(MQTT_TOPIC_COMMAND);
    if (mqttClient.subscribe(commandTopic.c_str())) {
      DEBUG_PRINT("Subscribed: ");
      DEBUG_PRINTLN(commandTopic);
    }
    
    String configTopic = buildDeviceTopic(MQTT_TOPIC_CONFIG);
    if (mqttClient.subscribe(configTopic.c_str())) {
      DEBUG_PRINT("Subscribed: ");
      DEBUG_PRINTLN(configTopic);
    }
    
    publishDeviceInfo();
    
    heartbeatTicker.detach();
    heartbeatTicker.attach_ms(HEARTBEAT_INTERVAL, publishHeartbeat);
    publishHeartbeat();

    displaySnapshotTicker.detach();
    displaySnapshotTicker.attach_ms(DISPLAY_SNAPSHOT_INTERVAL, publishDisplaySnapshot);
    publishDisplaySnapshot();
        
    return true;
  } else {
    DEBUG_PRINTLN("MQTT connection failed");
    DEBUG_PRINT("State: ");
    DEBUG_PRINTLN(mqttClient.state());
    setConnectivityState(true, false);
    mqttIsConnected = false;
    return false;
  }
}

void disconnectMQTT() {
  DEBUG_PRINTLN("Disconnecting from MQTT broker...");
  
  heartbeatTicker.detach();
  displaySnapshotTicker.detach();
  
  if (mqttIsConnected) {
    logEvent("mqtt_disconnect");
  }
  setEventLogReady(false);
  
  mqttClient.disconnect();
  wifiSecureClient.stop();
  
  setConnectivityState(WiFi.status() == WL_CONNECTED, false);
  mqttIsConnected = false;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

static unsigned long lastReconnectAttempt = 0;
static uint8_t reconnectAttempts = 0;
const uint8_t MAX_RECONNECT_LOG_ATTEMPTS = 5;

void resetMQTTReconnectTimer() {
  lastReconnectAttempt = 0;
  reconnectAttempts = 0;
  DEBUG_PRINTLN("MQTT reconnect timer reset");
}

void updateMQTT() {
  if (mqttIsConnected && !mqttClient.connected()) {
    DEBUG_PRINTLN("MQTT connection lost unexpectedly, cleaning up...");
    disconnectMQTT();
  }
  
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();
    
    unsigned long backoffInterval = 5000UL * (1UL << min(reconnectAttempts, (uint8_t)3));
    if (backoffInterval > 30000) backoffInterval = 30000;
    
    if (now - lastReconnectAttempt > backoffInterval) {
      lastReconnectAttempt = now;
      reconnectAttempts++;
      
      if (reconnectAttempts <= MAX_RECONNECT_LOG_ATTEMPTS) {
        DEBUG_PRINT("MQTT reconnect attempt ");
        DEBUG_PRINT(reconnectAttempts);
        DEBUG_PRINT(" (next in ");
        DEBUG_PRINT(backoffInterval / 1000);
        DEBUG_PRINTLN("s if fails)...");
      }
      
      if (connectMQTT()) {
        reconnectAttempts = 0;
        DEBUG_PRINTLN("MQTT reconnected successfully");
      } else {
        DEBUG_PRINT("MQTT reconnect failed, state: ");
        DEBUG_PRINTLN(mqttClient.state());
      }
    }
  }
  
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
}

// ============================================================================
// MQTT PUBLISHING HELPERS
// ============================================================================

String buildDeviceTopic(const char* basePath) {
  String topic = "norrtek-iot/";
  topic += getEnvironmentScope();
  topic += '/';
  topic += basePath;
  topic += '/';
  topic += getDeviceID();
  return topic;
}

String buildBaseTopic(const char* basePath) {
  String topic = "norrtek-iot/";
  topic += getEnvironmentScope();
  topic += '/';
  topic += basePath;
  return topic;
}

bool publishMqttPayload(const char* topic, const char* payload) {
  if (!mqttClient.connected()) {
    DEBUG_PRINTLN("MQTT not connected, cannot publish");
    return false;
  }
  
  if (mqttClient.publish(topic, payload)) {
    DEBUG_PRINT("Published to ");
    DEBUG_PRINT(topic);
    DEBUG_PRINT(": ");
    DEBUG_PRINTLN(payload);
    return true;
  } else {
    DEBUG_PRINT("Failed to publish to ");
    DEBUG_PRINTLN(topic);
    return false;
  }
}

bool publishMqttPayload(const String& topic, const char* payload) {
  return publishMqttPayload(topic.c_str(), payload);
}

bool publishMqttPayload(const String& topic, const String& payload) {
  return publishMqttPayload(topic.c_str(), payload.c_str());
}

// ============================================================================
// HEARTBEAT PUBLISHING
// ============================================================================

const int32_t RSSI_WARNING_THRESHOLD = -75;
const uint32_t HEAP_WARNING_THRESHOLD = 20000;

static bool lastRssiWasLow = false;
static bool lastHeapWasLow = false;

void publishHeartbeat() {
  if (!mqttClient.connected()) return;
  
  int32_t rssi = WiFi.RSSI();
  uint32_t freeHeap = ESP.getFreeHeap();
  
  bool rssiIsLow = (rssi < RSSI_WARNING_THRESHOLD);
  if (rssiIsLow && !lastRssiWasLow) {
    static char eventData[48];
    snprintf(eventData, sizeof(eventData), "{\"rssi\":%ld,\"threshold\":%ld}", rssi, RSSI_WARNING_THRESHOLD);
    logEvent("wifi_rssi_low", eventData);
  }
  lastRssiWasLow = rssiIsLow;
  
  bool heapIsLow = (freeHeap < HEAP_WARNING_THRESHOLD);
  if (heapIsLow && !lastHeapWasLow) {
    static char eventData[48];
    snprintf(eventData, sizeof(eventData), "{\"free_heap\":%lu,\"threshold\":%lu}", freeHeap, HEAP_WARNING_THRESHOLD);
    logEvent("low_heap_warning", eventData);
  }
  lastHeapWasLow = heapIsLow;
  
  static char payload[192];
  snprintf(payload, sizeof(payload),
    "{\"uptime\":%lu,\"rssi\":%ld,\"free_heap\":%lu,\"ssid\":\"%s\",\"ip\":\"%s\"}",
    millis() / 1000,
    rssi,
    freeHeap,
    WiFi.SSID().c_str(),
    WiFi.localIP().toString().c_str()
  );
  
  publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_HEARTBEAT), payload);
}

// ============================================================================
// DEVICE INFO PUBLISHING
// ============================================================================

void publishDeviceInfo() {
  if (!mqttClient.connected()) return;
  
  DEBUG_PRINTLN("Publishing device info...");
  
  static char payload[512];
  
  time_t currentTime = getCurrentTime();
  if (currentTime > 0) {
    snprintf(payload, sizeof(payload),
      "{\"product\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"environment\":\"%s\",\"config\":{\"temp_offset\":%.1f},\"supported_commands\":[\"temp_offset\",\"rollback\",\"restart\",\"snapshot\",\"info\",\"environment\"],\"timestamp\":%lu}",
      PRODUCT_NAME,
      getBoardType().c_str(),
      FIRMWARE_VERSION,
      getEnvironmentScope(),
      getTemperatureOffset(),
      (unsigned long)currentTime
    );
  } else {
    snprintf(payload, sizeof(payload),
      "{\"product\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"environment\":\"%s\",\"config\":{\"temp_offset\":%.1f},\"supported_commands\":[\"temp_offset\",\"rollback\",\"restart\",\"snapshot\",\"info\",\"environment\"]}",
      PRODUCT_NAME,
      getBoardType().c_str(),
      FIRMWARE_VERSION,
      getEnvironmentScope(),
      getTemperatureOffset()
    );
  }
  
  if (publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_INFO), payload)) {
    DEBUG_PRINTLN("Device info published successfully");
  }
}

// ============================================================================
// DISPLAY SNAPSHOT PUBLISHING
// ============================================================================

String base64Encode(const uint8_t* data, uint16_t length) {
  if (!data || length == 0) {
    return "";
  }
  
  uint16_t encodedLen = ((length + 2) / 3) * 4;
  String encoded;
  encoded.reserve(encodedLen + 1);
  
  uint16_t i = 0;
  while (i < length) {
    uint16_t remaining = length - i;
    
    uint32_t octet_a = data[i++];
    uint32_t octet_b = remaining > 1 ? data[i++] : 0;
    uint32_t octet_c = remaining > 2 ? data[i++] : 0;
    
    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
    
    encoded += (char)pgm_read_byte(&BASE64_CHARS[(triple >> 18) & 0x3F]);
    encoded += (char)pgm_read_byte(&BASE64_CHARS[(triple >> 12) & 0x3F]);
    encoded += (remaining > 1) ? (char)pgm_read_byte(&BASE64_CHARS[(triple >> 6) & 0x3F]) : '=';
    encoded += (remaining > 2) ? (char)pgm_read_byte(&BASE64_CHARS[triple & 0x3F]) : '=';
  }
  
  return encoded;
}

void publishDisplaySnapshot() {
  if (!mqttClient.connected()) {
    return;
  }
  
  DEBUG_PRINTLN("Publishing display snapshot...");
  
  DisplayConfig cfg = getDisplayConfig();
  
  if (cfg.rows == 0 || cfg.totalPixels == 0) {
    DEBUG_PRINTLN("Invalid display configuration, skipping snapshot");
    return;
  }
  
  createSnapshotBuffer();
  
  const uint8_t* buffer = getDisplayBuffer();
  uint16_t bufferSize = getDisplayBufferSize();
  
  if (bufferSize == 0 || bufferSize > 1024) {
    DEBUG_PRINTLN("Invalid buffer size, skipping snapshot");
    return;
  }
  
  static char monoColorStr[32];
  snprintf(monoColorStr, sizeof(monoColorStr), "[%u,%u,%u,%u]", 
           DISPLAY_COLOR_R, DISPLAY_COLOR_G, DISPLAY_COLOR_B, BRIGHTNESS);
  
  String payload = "{\"rows\":[";
  
  for (uint8_t i = 0; i < cfg.rows; i++) {
    if (i > 0) payload += ",";
    
    RowConfig& rowCfg = cfg.rowConfig[i];
    const char* text = getText(i);
    
    uint16_t rowPixels = rowCfg.width * rowCfg.height;
    uint16_t startBit = rowCfg.pixelOffset;
    uint16_t startByte = startBit / 8;
    uint16_t rowBytes = (rowPixels + 7) / 8;
    
    String rowBase64 = base64Encode(buffer + startByte, rowBytes);
    
    String escapedText = "";
    for (int j = 0; text[j] != '\0' && j < MAX_TEXT_LENGTH; j++) {
      if (text[j] == '"' || text[j] == '\\') {
        escapedText += "\\";
      }
      escapedText += text[j];
    }
    
    payload += "{\"width\":";
    payload += rowCfg.width;
    payload += ",\"height\":";
    payload += rowCfg.height;
    payload += ",\"scale\":";
    payload += rowCfg.scale;
    payload += ",\"monoColor\":";
    payload += monoColorStr;
    payload += ",\"text\":\"";
    payload += escapedText;
    payload += "\",\"pixels\":\"";
    payload += rowBase64;
    payload += "\"}";
  }
  
  payload += "]";
  
  time_t currentTime = getCurrentTime();
  if (currentTime > 0) {
    payload += ",\"timestamp\":";
    payload += (unsigned long)currentTime;
  }
  
  payload += "}";
  
  if (publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_DISPLAY_SNAPSHOT), payload)) {
    DEBUG_PRINTLN("Display snapshot published successfully");
  }
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

void handleRollbackCommand(String message) {
  int versionIndex = message.indexOf("\"version\"");
  if (versionIndex > 0) {
    int colonIndex = message.indexOf(":", versionIndex);
    int quoteStart = message.indexOf("\"", colonIndex + 1);
    int quoteEnd = message.indexOf("\"", quoteStart + 1);
    
    if (quoteStart > 0 && quoteEnd > quoteStart) {
      String version = message.substring(quoteStart + 1, quoteEnd);
      DEBUG_PRINT("Rolling back to version: ");
      DEBUG_PRINTLN(version);
      triggerOTAUpdate(version, true);
    }
  }
}

void handleRestartCommand() {
  DEBUG_PRINTLN("Restart command received, rebooting in 2 seconds...");
  logEvent("restart_command");
  delay(2000);
  ESP.restart();
}

// ============================================================================
// WIFI EVENT HANDLERS
// ============================================================================

static bool mqttConnectRequested = false;
static bool mqttDisconnectRequested = false;

void requestMQTTConnect() {
  mqttConnectRequested = true;
}

void requestMQTTDisconnect() {
  mqttDisconnectRequested = true;
}

void processDeferredMQTT() {
  if (mqttDisconnectRequested) {
    mqttDisconnectRequested = false;
    disconnectMQTT();
  }
  if (mqttConnectRequested) {
    mqttConnectRequested = false;
    DEBUG_PRINTLN("Processing deferred MQTT connect...");
    connectMQTT();
  }
}

#if defined(ESP32)
void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  DEBUG_PRINTLN("WiFi connected event");
  setConnectivityState(true, mqttIsConnected);
  resetMQTTReconnectTimer();
  requestMQTTConnect();
}

void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  DEBUG_PRINTLN("WiFi disconnected event");
  setConnectivityState(false, false);
  requestMQTTDisconnect();
}
#elif defined(ESP8266)
void onWiFiConnected(const WiFiEventStationModeGotIP& event) {
  DEBUG_PRINTLN("WiFi connected event");
  setConnectivityState(true, mqttIsConnected);
  resetMQTTReconnectTimer();
  requestMQTTConnect();
}

void onWiFiDisconnected(const WiFiEventStationModeDisconnected& event) {
  DEBUG_PRINTLN("WiFi disconnected event");
  setConnectivityState(false, false);
  requestMQTTDisconnect();
}
#endif
