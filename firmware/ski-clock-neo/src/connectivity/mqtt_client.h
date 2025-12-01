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
#include "core/debug.h"
#include "core/device_info.h"

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
extern const char MQTT_TOPIC_VERSION_RESPONSE[];
extern const char MQTT_TOPIC_COMMAND[];
extern const char MQTT_TOPIC_OTA_START[];
extern const char MQTT_TOPIC_OTA_PROGRESS[];
extern const char MQTT_TOPIC_OTA_COMPLETE[];
extern const char MQTT_TOPIC_DISPLAY_SNAPSHOT[];
extern const char MQTT_TOPIC_EVENTS[];

// MQTT client objects
extern WiFiClientSecure wifiSecureClient;
extern PubSubClient mqttClient;

// Heartbeat timing
extern const unsigned long HEARTBEAT_INTERVAL;
extern Ticker heartbeatTicker;
extern bool mqttIsConnected;

// Display snapshot timing
extern const unsigned long DISPLAY_SNAPSHOT_INTERVAL;
extern Ticker displaySnapshotTicker;

// Function declarations
void initMQTT();
bool connectMQTT();
void disconnectMQTT();
void updateMQTT();
void publishHeartbeat();
void publishDisplaySnapshot();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleRollbackCommand(String message);
void handleRestartCommand();
String base64Encode(const uint8_t* data, uint16_t length);

// MQTT publishing helpers (reduces code duplication across modules)
String buildDeviceTopic(const char* baseTopic);
bool publishMqttPayload(const char* topic, const char* payload);
bool publishMqttPayload(const String& topic, const char* payload);
bool publishMqttPayload(const String& topic, const String& payload);

// WiFi event handlers
#if defined(ESP32)
  void onWiFiConnected(WiFiEvent_t event, WiFiEventInfo_t info);
  void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
#elif defined(ESP8266)
  void onWiFiConnected(const WiFiEventStationModeGotIP& event);
  void onWiFiDisconnected(const WiFiEventStationModeDisconnected& event);
#endif

#endif
