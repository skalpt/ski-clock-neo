#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecure.h>
#else
  #error "This code requires ESP32 or ESP8266"
#endif

#include <PubSubClient.h>
#include <Ticker.h>
#include "debug.h"
#include "ota_update.h"  // For triggerOTAUpdate

// MQTT broker configuration (injected at build time)
#ifndef MQTT_HOST
  #error "MQTT_HOST must be defined at compile time via -DMQTT_HOST=\"...\""
#endif

#ifndef MQTT_USERNAME
  #error "MQTT_HOST must be defined at compile time via -DMQTT_USERNAME=\"...\""
#endif

#ifndef MQTT_PASSWORD
  #error "MQTT_PASSWORD must be defined at compile time via -DMQTT_PASSWORD=\"...\""
#endif

const uint16_t MQTT_PORT = 8883;  // TLS port for HiveMQ Cloud

// MQTT topics
#define MQTT_TOPIC_HEARTBEAT "skiclock/heartbeat"
#define MQTT_TOPIC_VERSION_UPDATES "skiclock/version/updates"
#define MQTT_TOPIC_VERSION_REQUEST "skiclock/version/request"
#define MQTT_TOPIC_VERSION_RESPONSE "skiclock/version/response"
#define MQTT_TOPIC_COMMAND "skiclock/command"
#define MQTT_TOPIC_OTA_START "skiclock/ota/start"
#define MQTT_TOPIC_OTA_PROGRESS "skiclock/ota/progress"
#define MQTT_TOPIC_OTA_COMPLETE "skiclock/ota/complete"

// MQTT client objects
WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

// Heartbeat timing
const unsigned long HEARTBEAT_INTERVAL = 60000;  // 60 seconds
Ticker heartbeatTicker;  // Software ticker for heartbeat updates
bool mqttIsConnected = false;  // Track MQTT connection state

// Version request timing
const unsigned long VERSION_REQUEST_INTERVAL = 3600000;  // 1 hour in milliseconds
Ticker versionRequestTicker;  // Software ticker for version requests

// Get unique device ID
String getDeviceID() {
  #if defined(ESP32)
    uint64_t chipid = ESP.getEfuseMac();
    return String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
  #elif defined(ESP8266)
    return String(ESP.getChipId(), HEX);
  #endif
}

// Get board type as string
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

// Forward declarations
bool connectMQTT();
void disconnectMQTT();
void publishHeartbeat();
void publishVersionRequest();
void handleRollbackCommand(String message);
void handleRestartCommand();

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
    // Publish version request to get latest version info
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
  
  // Configure TLS without certificate validation (works offline, no NTP required)
  wifiSecureClient.setInsecure();
  
  // Configure MQTT client
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);  // Increase buffer for JSON messages
  // Keep-alive defaults to 15 seconds (PubSubClient library default)
  
  DEBUG_PRINT("MQTT broker: ");
  DEBUG_PRINT(MQTT_HOST);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(MQTT_PORT);
  DEBUG_PRINTLN("TLS encryption enabled (no cert validation)");

  // Initial connection attempt if WiFi already connected
  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }

  // Register WiFi event handlers for automatic MQTT lifecycle management
  #if defined(ESP32)
    WiFi.onEvent(onWiFiConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onWiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  #elif defined(ESP8266)
    static WiFiEventHandler gotIpHandler = WiFi.onStationModeGotIP(onWiFiConnected);
    static WiFiEventHandler disconnectedHandler = WiFi.onStationModeDisconnected(onWiFiDisconnected);
  #endif
}

// Connect to MQTT broker (non-blocking)
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

    // Start heartbeat ticker only if not already running
    heartbeatTicker.detach();  // Detach first to prevent duplicates
    heartbeatTicker.attach_ms(HEARTBEAT_INTERVAL, publishHeartbeat);
    publishHeartbeat();  // Publish initial heartbeat immediately

    // Subscribe to version update broadcast topic
    if (mqttClient.subscribe(MQTT_TOPIC_VERSION_UPDATES)) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(MQTT_TOPIC_VERSION_UPDATES);
    } else {
      DEBUG_PRINTLN("Failed to subscribe to version updates topic");
    }
    
    // Subscribe to device-specific version response topic
    String versionResponseTopic = String(MQTT_TOPIC_VERSION_RESPONSE) + "/" + getDeviceID();
    if (mqttClient.subscribe(versionResponseTopic.c_str())) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(versionResponseTopic);
    } else {
      DEBUG_PRINTLN("Failed to subscribe to version response topic");
    }
    
    // Subscribe to device-specific command topic
    String commandTopic = String(MQTT_TOPIC_COMMAND) + "/" + getDeviceID();
    if (mqttClient.subscribe(commandTopic.c_str())) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(commandTopic);
    } else {
      DEBUG_PRINTLN("Failed to subscribe to command topic");
    }
    
    // Start version request ticker only if not already running
    versionRequestTicker.detach();  // Detach first to prevent duplicates
    versionRequestTicker.attach_ms(VERSION_REQUEST_INTERVAL, publishVersionRequest);
    publishVersionRequest();  // Publish initial version request immediately in case a new version was released while we were offline
    
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
  
  // Build JSON heartbeat payload (includes network info: SSID and IP)
  char payload[384];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"uptime\":%lu,\"rssi\":%d,\"free_heap\":%u,\"ssid\":\"%s\",\"ip\":\"%s\"}",
    getDeviceID().c_str(),
    getBoardType().c_str(),
    FIRMWARE_VERSION,
    millis() / 1000,  // uptime in seconds
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
  
  // Build JSON version request payload
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

// Disconnect from MQTT broker and stop tickers
void disconnectMQTT() {
  if (mqttIsConnected) {
    DEBUG_PRINTLN("Disconnecting from MQTT broker...");
    heartbeatTicker.detach();  // Stop heartbeat ticker
    versionRequestTicker.detach();  // Stop version request ticker
    mqttClient.disconnect();   // Clean disconnect
    mqttIsConnected = false;
  }
}

// Main MQTT loop (call frequently from main loop)
void updateMQTT() {
  // Check for connection loss (broker disconnect while WiFi remains up)
  if (mqttIsConnected && !mqttClient.connected()) {
    // Connection was lost, clean up
    DEBUG_PRINTLN("MQTT connection lost unexpectedly, cleaning up...");
    heartbeatTicker.detach();
    mqttIsConnected = false;
  }
  
  // Attempt reconnection if WiFi is up but MQTT is down
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {  // Retry every 5 seconds
      lastReconnectAttempt = now;
      DEBUG_PRINTLN("Attempting MQTT reconnection...");
      connectMQTT();
    }
  }
  
  // Process incoming messages if connected (must be called regularly)
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
  // ESP32: Use partition switching for instant rollback
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
  
  // Set boot partition to the other one
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
  // ESP8266: Re-download previous version from server
  DEBUG_PRINTLN("ESP8266 rollback: Re-downloading previous firmware");
  
  // Parse target_version from message if provided
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
    // Trigger OTA update with previous version
    triggerOTAUpdate(targetVersion);
  } else {
    DEBUG_PRINTLN("No target version specified, cannot rollback");
  }
#endif
}

#endif
