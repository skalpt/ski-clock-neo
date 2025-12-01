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

#include "mqtt_client.h"               // This file's header
#include "ski-clock-neo_config.h"      // For importing main configuration (credentials, etc.)
#include "core/led_indicator.h"        // For LED status patterns when MQTT connection state changes
#include "ota_update.h"                // For OTA update publishing
#include "display/display_core.h"      // For display snapshot publishing
#include "core/event_log.h"            // For event log publishing

// ============================================================================
// CONSTANTS
// ============================================================================

const uint16_t MQTT_PORT = 8883;  // TLS port for HiveMQ Cloud

// MQTT topics (device-specific topics use buildDeviceTopic() which auto-adds trailing slash)
const char MQTT_TOPIC_HEARTBEAT[] = "skiclock/heartbeat";
const char MQTT_TOPIC_VERSION_RESPONSE[] = "skiclock/version/response";
const char MQTT_TOPIC_COMMAND[] = "skiclock/command";
const char MQTT_TOPIC_OTA_START[] = "skiclock/ota/start";
const char MQTT_TOPIC_OTA_PROGRESS[] = "skiclock/ota/progress";
const char MQTT_TOPIC_OTA_COMPLETE[] = "skiclock/ota/complete";
const char MQTT_TOPIC_DISPLAY_SNAPSHOT[] = "skiclock/display/snapshot";
const char MQTT_TOPIC_EVENTS[] = "skiclock/event";

// Timing constants
const unsigned long HEARTBEAT_INTERVAL = 60000;           // 60 seconds
const unsigned long DISPLAY_SNAPSHOT_INTERVAL = 3600000;  // 1 hour

// ============================================================================
// STATE VARIABLES
// ============================================================================

WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);
Ticker heartbeatTicker;
Ticker displaySnapshotTicker;
bool mqttIsConnected = false;

// Base64 encoding lookup table (stored in PROGMEM to save RAM)
static const char BASE64_CHARS[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void mqttCallback(char* topic, byte* payload, unsigned int length);
String base64Encode(const uint8_t* data, uint16_t length);

// ============================================================================
// MQTT MESSAGE CALLBACK
// ============================================================================

// Handle incoming MQTT messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  DEBUG_PRINT("MQTT message received on topic: ");
  DEBUG_PRINTLN(topic);
  
  // Convert payload to string
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  DEBUG_PRINT("Message: ");
  DEBUG_PRINTLN(message);
  
  // Handle device-specific version responses
  String versionResponseTopic = String(MQTT_TOPIC_VERSION_RESPONSE) + "/" + getDeviceID();
  if (strcmp(topic, versionResponseTopic.c_str()) == 0) {
    DEBUG_PRINTLN("Version response received!");
    
    // Simple JSON parsing to extract update_available and latest_version
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
          
          // Trigger OTA update
          triggerOTAUpdate(latestVersion);
        }
      }
    } else {
      DEBUG_PRINTLN("Firmware is up to date");
    }
  }
  
  // Handle device-specific command messages
  String commandTopic = String(MQTT_TOPIC_COMMAND) + "/" + getDeviceID();
  if (strcmp(topic, commandTopic.c_str()) == 0) {
    DEBUG_PRINTLN("Command received!");
    
    // Parse command type from JSON
    if (message.indexOf("\"command\": \"rollback\"") > 0 || message.indexOf("\"command\":\"rollback\"") > 0) {
      DEBUG_PRINTLN("Executing rollback command");
      handleRollbackCommand(message);
    } else if (message.indexOf("\"command\": \"restart\"") > 0 || message.indexOf("\"command\":\"restart\"") > 0) {
      DEBUG_PRINTLN("Executing restart command");
      handleRestartCommand();
    } else if (message.indexOf("\"command\": \"snapshot\"") > 0 || message.indexOf("\"command\":\"snapshot\"") > 0) {
      DEBUG_PRINTLN("Executing snapshot command");
      publishDisplaySnapshot();
    } else {
      DEBUG_PRINTLN("Unknown command type");
    }
  }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initMQTT() {
  DEBUG_PRINTLN("Initializing MQTT client...");
  
  // Configure TLS without certificate validation
  wifiSecureClient.setInsecure();
  
  // Configure MQTT client
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);  // Increased for display snapshots
  
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
  
  // Generate unique client ID
  String clientId = "SkiClock-" + getDeviceID();
  
  // Attempt connection
  if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
    DEBUG_PRINTLN("MQTT broker connected!");
    setConnectivityState(true, true);
    mqttIsConnected = true;

    // Log mqtt_connect event and flush any queued events
    logEvent("mqtt_connect");
    setEventLogReady(true);
    flushEventQueue();

    // Subscribe to topics
    String versionResponseTopic = String(MQTT_TOPIC_VERSION_RESPONSE) + "/" + getDeviceID();
    if (mqttClient.subscribe(versionResponseTopic.c_str())) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(versionResponseTopic);
    }
    
    String commandTopic = String(MQTT_TOPIC_COMMAND) + "/" + getDeviceID();
    if (mqttClient.subscribe(commandTopic.c_str())) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(commandTopic);
    }
    
    // Start heartbeat ticker
    heartbeatTicker.detach();
    heartbeatTicker.attach_ms(HEARTBEAT_INTERVAL, publishHeartbeat);
    publishHeartbeat();

    // Start display snapshot ticker (hourly)
    displaySnapshotTicker.detach();
    displaySnapshotTicker.attach_ms(DISPLAY_SNAPSHOT_INTERVAL, publishDisplaySnapshot);
    publishDisplaySnapshot();
        
    return true;
  } else {
    DEBUG_PRINT("MQTT broker connection failed, rc=");
    DEBUG_PRINTLN(mqttClient.state());
    setConnectivityState(true, false);
    mqttIsConnected = false;
    return false;
  }
}

// Disconnect from MQTT broker
void disconnectMQTT() {
  if (mqttIsConnected) {
    DEBUG_PRINTLN("Disconnecting from MQTT broker...");
    logEvent("mqtt_disconnect");
    setEventLogReady(false);
    heartbeatTicker.detach();
    displaySnapshotTicker.detach();
    mqttClient.disconnect();
    setConnectivityState(WiFi.status() == WL_CONNECTED, false);
    mqttIsConnected = false;
  }
}

// ============================================================================
// MAIN LOOP
// ============================================================================

// Main MQTT update loop (call from main loop)
void updateMQTT() {
  // Check for connection loss
  if (mqttIsConnected && !mqttClient.connected()) {
    DEBUG_PRINTLN("MQTT connection lost unexpectedly, cleaning up...");
    disconnectMQTT();
  }
  
  // Attempt reconnection
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      DEBUG_PRINTLN("Attempting MQTT reconnection...");
      connectMQTT();
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

// Build a device-specific topic by appending device ID to base topic
String buildDeviceTopic(const char* baseTopic) {
  String topic = String(baseTopic);
  
  // Auto-insert trailing slash if base topic lacks one
  if (topic.length() > 0 && topic.charAt(topic.length() - 1) != '/') {
    topic += '/';
  }
  
  return topic + getDeviceID();
}

// Publish payload to topic with connection check and logging (char* overloads)
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
  
  // Build JSON payload
  static char payload[384];
  snprintf(payload, sizeof(payload),
    "{\"board\":\"%s\",\"version\":\"%s\",\"uptime\":%lu,\"rssi\":%ld,\"free_heap\":%lu,\"ssid\":\"%s\",\"ip\":\"%s\"}",
    getBoardType().c_str(),
    FIRMWARE_VERSION,
    millis() / 1000,
    rssi,
    freeHeap,
    WiFi.SSID().c_str(),
    WiFi.localIP().toString().c_str()
  );
  
  publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_HEARTBEAT), payload);
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
  
  // Build JSON payload with per-row structure
  // Format: {"rows":[{row0},{row1},...]}
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
  
  payload += "]}";
  
  // Check payload size
  if (payload.length() > 2000) {
    DEBUG_PRINT("Payload too large: ");
    DEBUG_PRINT(payload.length());
    DEBUG_PRINTLN(" bytes (max 2000)");
    return;
  }
  
  // Publish to device-specific snapshot topic
  if (publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_DISPLAY_SNAPSHOT), payload)) {
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
