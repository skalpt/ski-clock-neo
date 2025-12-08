#ifndef NORRTEK_MQTT_CLIENT_H
#define NORRTEK_MQTT_CLIENT_H

// Include product config for local build credentials
#include "../../ski-clock-neo_config.h"

#if defined(ESP32)
  #include <WiFi.h>
  #include <WiFiClientSecure.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <WiFiClientSecure.h>
#else
  #error "This code requires ESP32 or ESP8266"
#endif

#include <MQTT.h>
#include <Ticker.h>
#include "../core/debug.h"
#include "../core/device_info.h"

// MQTT broker configuration (injected at build time)
#ifndef MQTT_HOST
  #error "MQTT_HOST must be defined at compile time via -DMQTT_HOST=\"...\""
#endif

#ifndef MQTT_USERNAME
  #error "MQTT_USERNAME must be defined at compile time via -DMQTT_USERNAME=\"...\""
#endif

#ifndef MQTT_PASSWORD
  #error "MQTT_PASSWORD must be defined at compile time via -DMQTT_PASSWORD=\"...\""
#endif

// MQTT broker port
extern const uint16_t MQTT_PORT;

// MQTT topics (constexpr for compile-time optimization)
extern const char MQTT_TOPIC_HEARTBEAT[];
extern const char MQTT_TOPIC_INFO[];
extern const char MQTT_TOPIC_VERSION_RESPONSE[];
extern const char MQTT_TOPIC_COMMAND[];
extern const char MQTT_TOPIC_OTA_START[];
extern const char MQTT_TOPIC_OTA_PROGRESS[];
extern const char MQTT_TOPIC_OTA_COMPLETE[];
extern const char MQTT_TOPIC_DISPLAY_SNAPSHOT[];
extern const char MQTT_TOPIC_EVENTS[];

// MQTT client objects
extern WiFiClientSecure wifiSecureClient;
extern MQTTClient mqttClient;

// Heartbeat timing
extern const unsigned long HEARTBEAT_INTERVAL;
extern Ticker heartbeatTicker;
extern bool mqttIsConnected;

// Deferred connection flags (set from WiFi event handlers, processed in main loop)
// WiFi event handlers run in callback context with limited stack - don't call heavy
// operations (TLS/MQTT) directly. Instead, set flags and let updateMQTT() handle them.
extern volatile bool mqttConnectionRequested;
extern volatile bool mqttDisconnectionRequested;

// Request MQTT connect/disconnect (safe to call from WiFi event handlers)
void requestMQTTConnect();
void requestMQTTDisconnect();

// Display snapshot timing
extern const unsigned long DISPLAY_SNAPSHOT_INTERVAL;
extern Ticker displaySnapshotTicker;

// Function declarations
void initMQTT();
bool connectMQTT();
void disconnectMQTT();
void updateMQTT();
void resetMQTTReconnectTimer();
void publishHeartbeat();
void publishDeviceInfo();
void publishDisplaySnapshot();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleRollbackCommand(String message);
void handleRestartCommand();
String base64Encode(const uint8_t* data, uint16_t length);

// MQTT publishing helpers (reduces code duplication across modules)
// QoS 0 = fire-and-forget (for heartbeats), QoS 1 = guaranteed delivery (default)
String buildDeviceTopic(const char* baseTopic);
bool publishMqttPayload(const char* topic, const char* payload, int qos = 1);
bool publishMqttPayload(const String& topic, const char* payload, int qos = 1);
bool publishMqttPayload(const String& topic, const String& payload, int qos = 1);

// WiFi event handlers
#if defined(ESP32)
  void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info);
  void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
#elif defined(ESP8266)
  void onWiFiConnected(const WiFiEventStationModeGotIP& event);
  void onWiFiDisconnected(const WiFiEventStationModeDisconnected& event);
#endif

#endif // NORRTEK_MQTT_CLIENT_H
