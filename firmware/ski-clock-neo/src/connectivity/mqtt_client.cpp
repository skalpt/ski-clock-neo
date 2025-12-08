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

#include "mqtt_client.h"               // This file's header (includes MQTT.h)
#include "../../ski-clock-neo_config.h" // For importing main configuration (credentials, etc.)
#include "../core/led_indicator.h"     // For LED status patterns when MQTT connection state changes
#include "../core/device_config.h"     // For device configuration handling
#include "../data/data_time.h"         // For timestamp functions
#include "ota_update.h"                // For OTA update publishing
#include "../display/display_core.h"   // For display snapshot publishing
#include "../core/event_log.h"         // For event log publishing

// ============================================================================
// CONSTANTS
// ============================================================================

const uint16_t MQTT_PORT = 8883;  // TLS port for HiveMQ Cloud

// MQTT topic base paths (environment scope is prepended dynamically)
// Format: norrtek-iot/{env}/{path}/{device_id}
// Example: norrtek-iot/prod/heartbeat/abc123
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

// Timing constants
const unsigned long HEARTBEAT_INTERVAL = 60000;           // 60 seconds
const unsigned long DISPLAY_SNAPSHOT_INTERVAL = 3600000;  // 1 hour

// ============================================================================
// STATE VARIABLES
// ============================================================================

WiFiClientSecure wifiSecureClient;
MQTTClient mqttClient(2048);  // Buffer size for display snapshots
Ticker heartbeatTicker;
Ticker displaySnapshotTicker;
bool mqttIsConnected = false;

// Base64 encoding lookup table (stored in PROGMEM to save RAM)
static const char BASE64_CHARS[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void mqttMessageHandler(String &topic, String &payload);
String base64Encode(const uint8_t* data, uint16_t length);

// ============================================================================
// MQTT MESSAGE CALLBACK
// ============================================================================

// Handle incoming MQTT messages (arduino-mqtt callback signature)
void mqttMessageHandler(String &topic, String &message) {
  DEBUG_PRINT("MQTT message received on topic: ");
  DEBUG_PRINTLN(topic);
  
  DEBUG_PRINT("Message: ");
  DEBUG_PRINTLN(message);
  
  // Handle device-specific version responses
  String versionResponseTopic = buildDeviceTopic(MQTT_TOPIC_VERSION_RESPONSE);
  if (topic == versionResponseTopic) {
    DEBUG_PRINTLN("Version response received!");
    
    // Simple JSON parsing to extract update_available, latest_version, and pinned flag
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
          
          // Check if this is a pinned version (allows downgrades)
          // Handle various JSON formats: "pinned":true, "pinned": true, "pinned" : true
          bool isPinned = false;
          int pinnedIndex = message.indexOf("\"pinned\"");
          if (pinnedIndex > 0) {
            // Find the value after the colon
            int pinnedColonIndex = message.indexOf(":", pinnedIndex);
            if (pinnedColonIndex > pinnedIndex) {
              // Check for "true" after the colon (case-insensitive, handles whitespace)
              String valueSection = message.substring(pinnedColonIndex + 1, pinnedColonIndex + 10);
              valueSection.toLowerCase();
              valueSection.trim();
              isPinned = valueSection.startsWith("true");
            }
          }
          if (isPinned) {
            DEBUG_PRINTLN("Device is pinned to this version");
          }
          
          // Trigger OTA update (isPinned allows downgrade)
          triggerOTAUpdate(latestVersion, isPinned);
        }
      }
    } else {
      DEBUG_PRINTLN("Firmware is up to date");
    }
  }
  
  // Handle device-specific command messages
  String commandTopic = buildDeviceTopic(MQTT_TOPIC_COMMAND);
  if (topic == commandTopic) {
    DEBUG_PRINTLN("Command received!");
    
    // Parse command - support both JSON format and simple string commands
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
  
  // Handle device-specific config messages
  String configTopic = buildDeviceTopic(MQTT_TOPIC_CONFIG);
  if (topic == configTopic) {
    DEBUG_PRINTLN("Config message received!");
    handleConfigMessage(message);
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initMQTT() {
  DEBUG_PRINTLN("Initializing MQTT client...");
  
  // Configure TLS without certificate validation
  wifiSecureClient.setInsecure();
  
  // Configure MQTT client (arduino-mqtt)
  mqttClient.begin(MQTT_HOST, MQTT_PORT, wifiSecureClient);
  mqttClient.onMessage(mqttMessageHandler);
  
  // Set keepAlive=60s, cleanSession=false for persistent sessions, timeout=10s
  mqttClient.setOptions(60, false, 10000);
  
  DEBUG_PRINT("MQTT broker: ");
  DEBUG_PRINT(MQTT_HOST);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(MQTT_PORT);
  DEBUG_PRINTLN("TLS encryption enabled (no cert validation)");

  // Initial connection attempt if WiFi already connected
  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

// Connect to MQTT broker
bool connectMQTT() {
  if (mqttClient.connected()) {
    setConnectivityState(true, true);
    return true;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("WiFi not connected, skipping MQTT connection");
    return false;
  }
  
  DEBUG_PRINTLN("Connecting to MQTT broker...");
  
  // Generate unique client ID (fixed ID required for persistent sessions)
  String clientId = "NorrtekDevice-" + getDeviceID();
  
  // Attempt connection (cleanSession is set via setOptions in initMQTT)
  // arduino-mqtt connect signature: (clientId, username, password)
  if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD))
  {
    DEBUG_PRINTLN("MQTT broker connected!");
    setConnectivityState(true, true);
    mqttIsConnected = true;

    // Log mqtt_connect event and flush any queued events
    logEvent("mqtt_connect");
    setEventLogReady(true);
    flushEventQueue();

    // Subscribe to topics with QoS 1 for message persistence
    // QoS 1 ensures commands are queued by broker while device is offline
    String versionResponseTopic = buildDeviceTopic(MQTT_TOPIC_VERSION_RESPONSE);
    if (mqttClient.subscribe(versionResponseTopic.c_str(), 1)) {  // QoS 1
      DEBUG_PRINT("Subscribed (QoS 1): ");
      DEBUG_PRINTLN(versionResponseTopic);
    }
    
    String commandTopic = buildDeviceTopic(MQTT_TOPIC_COMMAND);
    if (mqttClient.subscribe(commandTopic.c_str(), 1)) {  // QoS 1
      DEBUG_PRINT("Subscribed (QoS 1): ");
      DEBUG_PRINTLN(commandTopic);
    }
    
    String configTopic = buildDeviceTopic(MQTT_TOPIC_CONFIG);
    if (mqttClient.subscribe(configTopic.c_str(), 1)) {  // QoS 1
      DEBUG_PRINT("Subscribed (QoS 1): ");
      DEBUG_PRINTLN(configTopic);
    }
    
    // Publish device info immediately on connect (static info + config)
    publishDeviceInfo();
    
    // Start heartbeat ticker (dynamic telemetry only)
    heartbeatTicker.detach();
    heartbeatTicker.attach_ms(HEARTBEAT_INTERVAL, publishHeartbeat);
    publishHeartbeat();

    // Start display snapshot ticker (hourly)
    displaySnapshotTicker.detach();
    displaySnapshotTicker.attach_ms(DISPLAY_SNAPSHOT_INTERVAL, publishDisplaySnapshot);
    publishDisplaySnapshot();
        
    return true;
  } else {
    DEBUG_PRINTLN("MQTT broker connection failed");
    DEBUG_PRINT("Last error: ");
    DEBUG_PRINTLN(mqttClient.lastError());
    setConnectivityState(true, false);
    mqttIsConnected = false;
    return false;
  }
}

// Disconnect from MQTT broker
// Called when WiFi drops or when we detect connection loss
void disconnectMQTT() {
  // Always clean up resources, even if we think we're not connected
  // This ensures a clean state for reconnection
  DEBUG_PRINTLN("Disconnecting from MQTT broker...");
  
  // Detach tickers first to prevent callbacks during cleanup
  heartbeatTicker.detach();
  displaySnapshotTicker.detach();
  
  // Only log event if we were actually connected (avoid spam)
  if (mqttIsConnected) {
    logEvent("mqtt_disconnect");
  }
  setEventLogReady(false);
  
  // Disconnect MQTT client
  mqttClient.disconnect();
  
  // Force-close the TLS socket to ensure clean state
  // This is critical: without this, the socket can be in a bad state
  // after WiFi drops and reconnects, preventing new connections
  wifiSecureClient.stop();
  
  setConnectivityState(WiFi.status() == WL_CONNECTED, false);
  mqttIsConnected = false;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

// Reconnection state
static unsigned long lastReconnectAttempt = 0;
static uint8_t reconnectAttempts = 0;
const uint8_t MAX_RECONNECT_LOG_ATTEMPTS = 5;  // Only log first N attempts to avoid spam

// Reset reconnect timer (called after WiFi reconnects)
void resetMQTTReconnectTimer() {
  lastReconnectAttempt = 0;
  reconnectAttempts = 0;
  DEBUG_PRINTLN("MQTT reconnect timer reset");
}

// Main MQTT update loop (call from main loop)
void updateMQTT() {
  // Check for connection loss
  if (mqttIsConnected && !mqttClient.connected()) {
    DEBUG_PRINTLN("MQTT connection lost unexpectedly, cleaning up...");
    disconnectMQTT();
  }
  
  // Attempt reconnection with exponential backoff
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();
    
    // Calculate backoff interval: 5s, 10s, 20s, 30s (capped)
    unsigned long backoffInterval = 5000UL * (1UL << min(reconnectAttempts, (uint8_t)3));
    if (backoffInterval > 30000) backoffInterval = 30000;
    
    if (now - lastReconnectAttempt > backoffInterval) {
      lastReconnectAttempt = now;
      reconnectAttempts++;
      
      // Log first few attempts, then reduce verbosity
      if (reconnectAttempts <= MAX_RECONNECT_LOG_ATTEMPTS) {
        DEBUG_PRINT("MQTT reconnect attempt ");
        DEBUG_PRINT(reconnectAttempts);
        DEBUG_PRINT(" (next in ");
        DEBUG_PRINT(backoffInterval / 1000);
        DEBUG_PRINTLN("s if fails)...");
      }
      
      if (connectMQTT()) {
        // Success! Reset counter
        reconnectAttempts = 0;
        DEBUG_PRINTLN("MQTT reconnected successfully");
      } else {
        DEBUG_PRINT("MQTT reconnect failed, error: ");
        DEBUG_PRINTLN(mqttClient.lastError());
      }
    }
  }
  
  // Process incoming messages
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
}

// ============================================================================
// MQTT PUBLISHING HELPERS
// ============================================================================

// Build a device-specific topic with environment scope
// Format: norrtek-iot/{env}/{path}/{device_id}
// Example: norrtek-iot/prod/heartbeat/abc123
String buildDeviceTopic(const char* basePath) {
  String topic = "norrtek-iot/";
  topic += getEnvironmentScope();
  topic += '/';
  topic += basePath;
  topic += '/';
  topic += getDeviceID();
  return topic;
}

// Build a topic without device ID suffix (for subscriptions with wildcards)
// Format: norrtek-iot/{env}/{path}
String buildBaseTopic(const char* basePath) {
  String topic = "norrtek-iot/";
  topic += getEnvironmentScope();
  topic += '/';
  topic += basePath;
  return topic;
}

// Publish payload to topic with connection check, QoS support, and logging
// QoS 0 = fire-and-forget (for heartbeats)
// QoS 1 = guaranteed delivery (default for everything else)
bool publishMqttPayload(const char* topic, const char* payload, int qos) {
  if (!mqttClient.connected()) {
    DEBUG_PRINTLN("MQTT not connected, cannot publish");
    return false;
  }
  
  // arduino-mqtt publish signature: (topic, payload, retained, qos)
  if (mqttClient.publish(topic, payload, false, qos)) {
    DEBUG_PRINT("Published (QoS ");
    DEBUG_PRINT(qos);
    DEBUG_PRINT(") to ");
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

bool publishMqttPayload(const String& topic, const char* payload, int qos) {
  return publishMqttPayload(topic.c_str(), payload, qos);
}

bool publishMqttPayload(const String& topic, const String& payload, int qos) {
  return publishMqttPayload(topic.c_str(), payload.c_str(), qos);
}

// ============================================================================
// HEARTBEAT PUBLISHING
// ============================================================================

// Thresholds for warning events (only log once per crossing)
const int32_t RSSI_WARNING_THRESHOLD = -75;     // dBm - weak signal
const uint32_t HEAP_WARNING_THRESHOLD = 20000;  // bytes - low memory

static bool lastRssiWasLow = false;
static bool lastHeapWasLow = false;

void publishHeartbeat() {
  if (!mqttClient.connected()) return;
  
  int32_t rssi = WiFi.RSSI();
  uint32_t freeHeap = ESP.getFreeHeap();
  
  // Check for low RSSI (log only when crossing threshold)
  bool rssiIsLow = (rssi < RSSI_WARNING_THRESHOLD);
  if (rssiIsLow && !lastRssiWasLow) {
    static char eventData[48];
    snprintf(eventData, sizeof(eventData), "{\"rssi\":%ld,\"threshold\":%ld}", rssi, RSSI_WARNING_THRESHOLD);
    logEvent("wifi_rssi_low", eventData);
  }
  lastRssiWasLow = rssiIsLow;
  
  // Check for low heap (log only when crossing threshold)
  bool heapIsLow = (freeHeap < HEAP_WARNING_THRESHOLD);
  if (heapIsLow && !lastHeapWasLow) {
    static char eventData[48];
    snprintf(eventData, sizeof(eventData), "{\"free_heap\":%lu,\"threshold\":%lu}", freeHeap, HEAP_WARNING_THRESHOLD);
    logEvent("low_heap_warning", eventData);
  }
  lastHeapWasLow = heapIsLow;
  
  // Build slimmed-down JSON payload with only dynamic telemetry
  // Static device info (product, board, version, config) is now sent via /info topic
  static char payload[192];
  snprintf(payload, sizeof(payload),
    "{\"uptime\":%lu,\"rssi\":%ld,\"free_heap\":%lu,\"ssid\":\"%s\",\"ip\":\"%s\"}",
    millis() / 1000,
    rssi,
    freeHeap,
    WiFi.SSID().c_str(),
    WiFi.localIP().toString().c_str()
  );
  
  // Heartbeats use QoS 0 (fire-and-forget) - frequent, only latest matters
  publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_HEARTBEAT), payload, 0);
}

// ============================================================================
// DEVICE INFO PUBLISHING
// ============================================================================

void publishDeviceInfo() {
  if (!mqttClient.connected()) return;
  
  DEBUG_PRINTLN("Publishing device info...");
  
  // Build JSON payload with static device info, config, and supported commands
  // Include environment scope so dashboard knows which namespace the device uses
  // Include timestamp if time is synced, otherwise omit (dashboard will use receive time)
  static char payload[512];
  
  time_t currentTime = getCurrentTime();
  if (currentTime > 0) {
    // Time is synced - include timestamp
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
    // No time sync - omit timestamp (dashboard uses receive time)
    snprintf(payload, sizeof(payload),
      "{\"product\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"environment\":\"%s\",\"config\":{\"temp_offset\":%.1f},\"supported_commands\":[\"temp_offset\",\"rollback\",\"restart\",\"snapshot\",\"info\",\"environment\"]}",
      PRODUCT_NAME,
      getBoardType().c_str(),
      FIRMWARE_VERSION,
      getEnvironmentScope(),
      getTemperatureOffset()
    );
  }
  
  // Device info uses QoS 1 for guaranteed delivery
  if (publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_INFO), payload, 1)) {
    DEBUG_PRINTLN("Device info published successfully");
  }
}

// ============================================================================
// DISPLAY SNAPSHOT PUBLISHING
// ============================================================================

// Base64 encode binary data
String base64Encode(const uint8_t* data, uint16_t length) {
  if (!data || length == 0) {
    return "";
  }
  
  // Pre-allocate string to avoid reallocations
  uint16_t encodedLen = ((length + 2) / 3) * 4;
  String encoded;
  encoded.reserve(encodedLen + 1);
  
  uint16_t i = 0;
  while (i < length) {
    uint16_t remaining = length - i;
    
    // Read 3 bytes (or less for final block)
    uint32_t octet_a = data[i++];
    uint32_t octet_b = remaining > 1 ? data[i++] : 0;
    uint32_t octet_c = remaining > 2 ? data[i++] : 0;
    
    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
    
    // Output 4 characters
    encoded += (char)pgm_read_byte(&BASE64_CHARS[(triple >> 18) & 0x3F]);
    encoded += (char)pgm_read_byte(&BASE64_CHARS[(triple >> 12) & 0x3F]);
    encoded += (remaining > 1) ? (char)pgm_read_byte(&BASE64_CHARS[(triple >> 6) & 0x3F]) : '=';
    encoded += (remaining > 2) ? (char)pgm_read_byte(&BASE64_CHARS[triple & 0x3F]) : '=';
  }
  
  return encoded;
}

// Publish display snapshot to MQTT with per-row structure
void publishDisplaySnapshot() {
  if (!mqttClient.connected()) {
    return;
  }
  
  DEBUG_PRINTLN("Publishing display snapshot...");
  
  // Get display configuration
  DisplayConfig cfg = getDisplayConfig();
  
  // Validate configuration
  if (cfg.rows == 0 || cfg.totalPixels == 0) {
    DEBUG_PRINTLN("Invalid display configuration, skipping snapshot");
    return;
  }
  
  // Create snapshot buffer from current NeoPixel state
  createSnapshotBuffer();
  
  // Get display buffer
  const uint8_t* buffer = getDisplayBuffer();
  uint16_t bufferSize = getDisplayBufferSize();
  
  if (bufferSize == 0 || bufferSize > 1024) {
    DEBUG_PRINTLN("Invalid buffer size, skipping snapshot");
    return;
  }
  
  // Build monoColor array [R, G, B, brightness] - shared across all rows
  static char monoColorStr[32];
  snprintf(monoColorStr, sizeof(monoColorStr), "[%u,%u,%u,%u]", 
           DISPLAY_COLOR_R, DISPLAY_COLOR_G, DISPLAY_COLOR_B, BRIGHTNESS);
  
  // Build JSON payload with per-row structure and timestamp
  // Format: {"rows":[{row0},{row1},...], "timestamp": 1234567890}
  String payload = "{\"rows\":[";
  
  for (uint8_t i = 0; i < cfg.rows; i++) {
    if (i > 0) payload += ",";
    
    RowConfig& rowCfg = cfg.rowConfig[i];
    const char* text = getText(i);
    
    // Calculate byte range for this row's pixels
    uint16_t rowPixels = rowCfg.width * rowCfg.height;
    uint16_t startBit = rowCfg.pixelOffset;
    uint16_t startByte = startBit / 8;
    uint16_t rowBytes = (rowPixels + 7) / 8;
    
    // Encode this row's pixel data to base64
    String rowBase64 = base64Encode(buffer + startByte, rowBytes);
    
    // Build escaped text string
    String escapedText = "";
    for (int j = 0; text[j] != '\0' && j < MAX_TEXT_LENGTH; j++) {
      if (text[j] == '"' || text[j] == '\\') {
        escapedText += "\\";
      }
      escapedText += text[j];
    }
    
    // Build row JSON object
    payload += "{\"text\":\"" + escapedText + "\"";
    payload += ",\"cols\":" + String(rowCfg.panels);
    payload += ",\"width\":" + String(rowCfg.width);
    payload += ",\"height\":" + String(rowCfg.height);
    payload += ",\"mono\":\"" + rowBase64 + "\"";
    payload += ",\"monoColor\":" + String(monoColorStr);
    payload += "}";
  }
  
  payload += "]";
  
  // Add timestamp if time is synced
  time_t currentTime = getCurrentTime();
  if (currentTime > 0) {
    payload += ",\"timestamp\":" + String((unsigned long)currentTime);
  }
  
  payload += "}";
  
  // Check payload size
  if (payload.length() > 2000) {
    DEBUG_PRINT("Payload too large: ");
    DEBUG_PRINT(payload.length());
    DEBUG_PRINTLN(" bytes (max 2000)");
    return;
  }
  
  // Publish to device-specific snapshot topic (QoS 1 for guaranteed delivery)
  if (publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_DISPLAY_SNAPSHOT), payload, 1)) {
    DEBUG_PRINT("Display snapshot size: ");
    DEBUG_PRINT(payload.length());
    DEBUG_PRINTLN(" bytes");
  }
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

// Handle restart command
void handleRestartCommand() {
  DEBUG_PRINTLN("Restart command received, rebooting in 2 seconds...");
  delay(2000);
  ESP.restart();
}

// Handle rollback command
void handleRollbackCommand(String message) {
  DEBUG_PRINTLN("Rollback command processing...");
  
#if defined(ESP32)
  // ESP32: Use partition switching
  DEBUG_PRINTLN("ESP32 rollback: Switching to previous partition");
  
  const esp_partition_t* current = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
  
  if (current == NULL || next == NULL) {
    DEBUG_PRINTLN("Error: Cannot get OTA partitions");
    return;
  }
  
  DEBUG_PRINT("Current partition: ");
  DEBUG_PRINTLN(current->label);
  DEBUG_PRINT("Switching to partition: ");
  DEBUG_PRINTLN(next->label);
  
  esp_err_t err = esp_ota_set_boot_partition(next);
  if (err != ESP_OK) {
    DEBUG_PRINT("Error setting boot partition: ");
    DEBUG_PRINTLN(esp_err_to_name(err));
    return;
  }
  
  DEBUG_PRINTLN("Boot partition set, rebooting in 2 seconds...");
  delay(2000);
  ESP.restart();
  
#elif defined(ESP8266)
  // ESP8266: Re-download previous version
  DEBUG_PRINTLN("ESP8266 rollback: Re-downloading previous firmware");
  
  String targetVersion = "";
  int versionIndex = message.indexOf("\"target_version\"");
  if (versionIndex > 0) {
    int colonIndex = message.indexOf(":", versionIndex);
    int quoteStart = message.indexOf("\"", colonIndex + 1);
    int quoteEnd = message.indexOf("\"", quoteStart + 1);
    
    if (quoteStart > 0 && quoteEnd > quoteStart) {
      targetVersion = message.substring(quoteStart + 1, quoteEnd);
    }
  }
  
  if (targetVersion.length() > 0 && targetVersion != "null") {
    DEBUG_PRINT("Rolling back to version: ");
    DEBUG_PRINTLN(targetVersion);
    triggerOTAUpdate(targetVersion);
  } else {
    DEBUG_PRINTLN("No target version specified, cannot rollback");
  }
#endif
}
