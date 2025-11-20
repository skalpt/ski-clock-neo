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

// MQTT client objects
WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

// Heartbeat timing
const unsigned long HEARTBEAT_INTERVAL = 60000;  // 60 seconds
Ticker heartbeatTicker;  // Software ticker for heartbeat updates
bool mqttIsConnected = false;  // Track MQTT connection state

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

// Forward declarations for WiFi event handlers
bool connectMQTT();
void disconnectMQTT();

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
  
  // Handle version update notifications
  if (strcmp(topic, MQTT_TOPIC_VERSION_UPDATES) == 0) {
    DEBUG_PRINTLN("Version update notification received!");
    // Trigger immediate OTA check (will be handled by main loop)
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

    // Subscribe to version update topic
    if (mqttClient.subscribe(MQTT_TOPIC_VERSION_UPDATES)) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(MQTT_TOPIC_VERSION_UPDATES);
    } else {
      DEBUG_PRINTLN("Failed to subscribe to version updates topic");
    }
    
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
  
  // Build JSON heartbeat payload
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"uptime\":%lu,\"rssi\":%d,\"free_heap\":%u}",
    getDeviceID().c_str(),
    getBoardType().c_str(),
    FIRMWARE_VERSION,
    millis() / 1000,  // uptime in seconds
    WiFi.RSSI(),
    ESP.getFreeHeap()
  );
  
  if (mqttClient.publish(MQTT_TOPIC_HEARTBEAT, payload)) {
    DEBUG_PRINT("Heartbeat published: ");
    DEBUG_PRINTLN(payload);
  } else {
    DEBUG_PRINTLN("Failed to publish heartbeat");
  }
}

// Disconnect from MQTT broker and stop heartbeat
void disconnectMQTT() {
  if (mqttIsConnected) {
    DEBUG_PRINTLN("Disconnecting from MQTT broker...");
    heartbeatTicker.detach();  // Stop heartbeat ticker
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

#endif
