#include "mqtt_client.h"
#include "ota_update.h"  // For triggerOTAUpdate
#include "display.h"     // For display snapshot

// MQTT broker port
const uint16_t MQTT_PORT = 8883;  // TLS port for HiveMQ Cloud

// MQTT topics (const arrays with external linkage)
const char MQTT_TOPIC_HEARTBEAT[] = "skiclock/heartbeat";
const char MQTT_TOPIC_VERSION_UPDATES[] = "skiclock/version/updates";
const char MQTT_TOPIC_VERSION_REQUEST[] = "skiclock/version/request";
const char MQTT_TOPIC_VERSION_RESPONSE[] = "skiclock/version/response";
const char MQTT_TOPIC_COMMAND[] = "skiclock/command";
const char MQTT_TOPIC_OTA_START[] = "skiclock/ota/start";
const char MQTT_TOPIC_OTA_PROGRESS[] = "skiclock/ota/progress";
const char MQTT_TOPIC_OTA_COMPLETE[] = "skiclock/ota/complete";
const char MQTT_TOPIC_DISPLAY_SNAPSHOT[] = "skiclock/display/snapshot";

// MQTT client objects
WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

// Heartbeat timing
const unsigned long HEARTBEAT_INTERVAL = 60000;  // 60 seconds
Ticker heartbeatTicker;
bool mqttIsConnected = false;

// Version request timing
const unsigned long VERSION_REQUEST_INTERVAL = 3600000;  // 1 hour
Ticker versionRequestTicker;

// Display snapshot timing
const unsigned long DISPLAY_SNAPSHOT_INTERVAL = 3600000;  // 1 hour
Ticker displaySnapshotTicker;

// MQTT message callback for incoming messages
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
  
  // Handle version update broadcast notifications
  if (strcmp(topic, MQTT_TOPIC_VERSION_UPDATES) == 0) {
    DEBUG_PRINTLN("Version update broadcast received!");
    publishVersionRequest();
  }
  
  // Handle device-specific version responses
  String versionResponseTopic = String(MQTT_TOPIC_VERSION_RESPONSE) + "/" + getDeviceID();
  if (strcmp(topic, versionResponseTopic.c_str()) == 0) {
    DEBUG_PRINTLN("Version response received!");
    
    // Simple JSON parsing to extract update_available and latest_version
    if (message.indexOf("\"update_available\":true") > 0) {
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
    if (message.indexOf("\"command\":\"rollback\"") > 0) {
      DEBUG_PRINTLN("Executing rollback command");
      handleRollbackCommand(message);
    } else if (message.indexOf("\"command\":\"restart\"") > 0) {
      DEBUG_PRINTLN("Executing restart command");
      handleRestartCommand();
    } else if (message.indexOf("\"command\":\"snapshot\"") > 0) {
      DEBUG_PRINTLN("Executing snapshot command");
      publishDisplaySnapshot();
    } else {
      DEBUG_PRINTLN("Unknown command type");
    }
  }
}

// WiFi event handlers for automatic MQTT lifecycle management
#if defined(ESP32)
  void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    DEBUG_PRINTLN("WiFi connected, connecting to MQTT...");
    connectMQTT();
  }
  
  void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    DEBUG_PRINTLN("WiFi disconnected, stopping MQTT...");
    disconnectMQTT();
  }
#elif defined(ESP8266)
  void onWiFiConnected(const WiFiEventStationModeGotIP& event) {
    DEBUG_PRINTLN("WiFi connected, connecting to MQTT...");
    connectMQTT();
  }
  
  void onWiFiDisconnected(const WiFiEventStationModeDisconnected& event) {
    DEBUG_PRINTLN("WiFi disconnected, stopping MQTT...");
    disconnectMQTT();
  }
#endif

// Initialize MQTT connection
void setupMQTT() {
  DEBUG_PRINTLN("Initializing MQTT client...");
  
  // Configure TLS without certificate validation
  wifiSecureClient.setInsecure();
  
  // Configure MQTT client
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);  // Increased for display snapshots (max ~342 bytes base64 for 16x16 panel)
  
  DEBUG_PRINT("MQTT broker: ");
  DEBUG_PRINT(MQTT_HOST);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(MQTT_PORT);
  DEBUG_PRINTLN("TLS encryption enabled (no cert validation)");

  // Initial connection attempt if WiFi already connected
  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }

  // Register WiFi event handlers
  #if defined(ESP32)
    WiFi.onEvent(onWiFiConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onWiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  #elif defined(ESP8266)
    static WiFiEventHandler gotIpHandler = WiFi.onStationModeGotIP(onWiFiConnected);
    static WiFiEventHandler disconnectedHandler = WiFi.onStationModeDisconnected(onWiFiDisconnected);
  #endif
}

// Connect to MQTT broker
bool connectMQTT() {
  if (mqttClient.connected()) {
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
    mqttIsConnected = true;

    // Start heartbeat ticker
    heartbeatTicker.detach();
    heartbeatTicker.attach_ms(HEARTBEAT_INTERVAL, publishHeartbeat);
    publishHeartbeat();

    // Subscribe to topics
    if (mqttClient.subscribe(MQTT_TOPIC_VERSION_UPDATES)) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(MQTT_TOPIC_VERSION_UPDATES);
    }
    
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
    
    // Start version request ticker
    versionRequestTicker.detach();
    versionRequestTicker.attach_ms(VERSION_REQUEST_INTERVAL, publishVersionRequest);
    publishVersionRequest();
    
    // Start display snapshot ticker (hourly)
    displaySnapshotTicker.detach();
    displaySnapshotTicker.attach_ms(DISPLAY_SNAPSHOT_INTERVAL, publishDisplaySnapshot);
    publishDisplaySnapshot();  // Send initial snapshot
    
    return true;
  } else {
    DEBUG_PRINT("MQTT broker connection failed, rc=");
    DEBUG_PRINTLN(mqttClient.state());
    mqttIsConnected = false;
    return false;
  }
}

// Publish heartbeat message
void publishHeartbeat() {
  if (!mqttClient.connected()) {
    return;
  }
  
  char payload[384];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"uptime\":%lu,\"rssi\":%d,\"free_heap\":%u,\"ssid\":\"%s\",\"ip\":\"%s\"}",
    getDeviceID().c_str(),
    getBoardType().c_str(),
    FIRMWARE_VERSION,
    millis() / 1000,
    WiFi.RSSI(),
    ESP.getFreeHeap(),
    WiFi.SSID().c_str(),
    WiFi.localIP().toString().c_str()
  );
  
  if (mqttClient.publish(MQTT_TOPIC_HEARTBEAT, payload)) {
    DEBUG_PRINT("Heartbeat published: ");
    DEBUG_PRINTLN(payload);
  } else {
    DEBUG_PRINTLN("Failed to publish heartbeat");
  }
}

// Publish version request message
void publishVersionRequest() {
  if (!mqttClient.connected()) {
    return;
  }
  
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"platform\":\"%s\",\"current_version\":\"%s\"}",
    getDeviceID().c_str(),
    getPlatform().c_str(),
    FIRMWARE_VERSION
  );
  
  if (mqttClient.publish(MQTT_TOPIC_VERSION_REQUEST, payload)) {
    DEBUG_PRINT("Version request published: ");
    DEBUG_PRINTLN(payload);
  } else {
    DEBUG_PRINTLN("Failed to publish version request");
  }
}

// Base64 encoding lookup table (stored in PROGMEM to save RAM)
static const char BASE64_CHARS[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 encode binary data with buffer safety
String base64Encode(const uint8_t* data, uint16_t length) {
  if (!data || length == 0) {
    return "";
  }
  
  // Pre-allocate string to avoid reallocations (4/3 expansion + padding)
  uint16_t encodedLen = ((length + 2) / 3) * 4;
  String encoded;
  encoded.reserve(encodedLen + 1);
  
  uint16_t i = 0;
  while (i < length) {
    // Read 3 bytes (or less for final block)
    uint32_t octet_a = i < length ? data[i++] : 0;
    uint32_t octet_b = i < length ? data[i++] : 0;
    uint32_t octet_c = i < length ? data[i++] : 0;
    
    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
    
    encoded += pgm_read_byte(&BASE64_CHARS[(triple >> 18) & 0x3F]);
    encoded += pgm_read_byte(&BASE64_CHARS[(triple >> 12) & 0x3F]);
    encoded += (i > length + 1) ? '=' : pgm_read_byte(&BASE64_CHARS[(triple >> 6) & 0x3F]);
    encoded += (i > length) ? '=' : pgm_read_byte(&BASE64_CHARS[triple & 0x3F]);
  }
  
  return encoded;
}

// Publish display snapshot
void publishDisplaySnapshot() {
  if (!mqttClient.connected()) {
    return;
  }
  
  DEBUG_PRINTLN("Publishing display snapshot...");
  
  // Get display configuration
  DisplayConfig cfg = getDisplayConfig();
  
  // Validate configuration before publishing
  if (cfg.rows == 0 || cfg.cols == 0 || cfg.width == 0 || cfg.height == 0) {
    DEBUG_PRINTLN("Invalid display configuration, skipping snapshot");
    return;
  }
  
  // Get display buffer
  const uint8_t* buffer = getDisplayBuffer();
  uint16_t bufferSize = getDisplayBufferSize();
  
  // Sanity check buffer size
  if (bufferSize == 0 || bufferSize > 512) {
    DEBUG_PRINTLN("Invalid buffer size, skipping snapshot");
    return;
  }
  
  // Encode buffer to base64
  String base64Data = base64Encode(buffer, bufferSize);
  
  // Build JSON payload with safe integer-to-string conversion
  // Format: {"device_id":"xxx","rows":1,"cols":1,"width":16,"height":16,"pixels":"base64data"}
  char rowsStr[8], colsStr[8], widthStr[8], heightStr[8];
  snprintf(rowsStr, sizeof(rowsStr), "%u", cfg.rows);
  snprintf(colsStr, sizeof(colsStr), "%u", cfg.cols);
  snprintf(widthStr, sizeof(widthStr), "%u", cfg.width);
  snprintf(heightStr, sizeof(heightStr), "%u", cfg.height);
  
  String payload = "{\"device_id\":\"" + getDeviceID() + 
                   "\",\"rows\":" + String(rowsStr) +
                   ",\"cols\":" + String(colsStr) +
                   ",\"width\":" + String(widthStr) +
                   ",\"height\":" + String(heightStr) +
                   ",\"pixels\":\"" + base64Data + "\"}";
  
  // Check payload size against MQTT buffer
  if (payload.length() > 2000) {
    DEBUG_PRINT("Payload too large: ");
    DEBUG_PRINT(payload.length());
    DEBUG_PRINTLN(" bytes (max 2000)");
    return;
  }
  
  // Publish to device-specific snapshot topic
  String snapshotTopic = String(MQTT_TOPIC_DISPLAY_SNAPSHOT) + "/" + getDeviceID();
  
  if (mqttClient.publish(snapshotTopic.c_str(), payload.c_str())) {
    DEBUG_PRINT("Display snapshot published (");
    DEBUG_PRINT(payload.length());
    DEBUG_PRINTLN(" bytes)");
  } else {
    DEBUG_PRINTLN("Failed to publish display snapshot");
  }
}

// Disconnect from MQTT broker
void disconnectMQTT() {
  if (mqttIsConnected) {
    DEBUG_PRINTLN("Disconnecting from MQTT broker...");
    heartbeatTicker.detach();
    versionRequestTicker.detach();
    displaySnapshotTicker.detach();
    mqttClient.disconnect();
    mqttIsConnected = false;
  }
}

// Main MQTT loop
void updateMQTT() {
  // Check for connection loss
  if (mqttIsConnected && !mqttClient.connected()) {
    DEBUG_PRINTLN("MQTT connection lost unexpectedly, cleaning up...");
    heartbeatTicker.detach();
    mqttIsConnected = false;
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
