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
#include "debug.h"

// MQTT broker configuration (injected at build time)
#ifndef MQTT_HOST
  #define MQTT_HOST "your-broker.hivemq.cloud"
#endif

#ifndef MQTT_USERNAME
  #define MQTT_USERNAME "your-username"
#endif

#ifndef MQTT_PASSWORD
  #define MQTT_PASSWORD "your-password"
#endif

const uint16_t MQTT_PORT = 8883;  // TLS port for HiveMQ Cloud

// MQTT topics
#define MQTT_TOPIC_HEARTBEAT "skiclock/heartbeat"
#define MQTT_TOPIC_VERSION_UPDATES "skiclock/version/updates"

// Let's Encrypt R3 Root CA Certificate (HiveMQ Cloud uses Let's Encrypt)
static const char* ROOT_CA_CERT PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFFjCCAv6gAwIBAgIRAJErCErPDBinU/bWLiWnX1owDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjAwOTA0MDAwMDAw
WhcNMjUwOTE1MTYwMDAwWjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg
RW5jcnlwdDELMAkGA1UEAxMCUjMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK
AoIBAQC7AhUozPaglNMPEuyNVZLD+ILxmaZ6QoinXSaqtSu5xUyxr45r+XXIo9cP
R5QUVTVXjJ6oojkZ9YI8QqlObvU7wy7bjcCwXPNZOOftz2nwWgsbvsCUJCWH+jdx
sxPnHKzhm+/b5DtFUkWWqcFTzjTIUu61ru2P3mBw4qVUq7ZtDpelQDRrK9O8Zutm
NHz6a4uPVymZ+DAXXbpyb/uBxa3Shlg9F8fnCbvxK/eG3MHacV3URuPMrSXBiLxg
Z3Vms/EY96Jc5lP/Ooi2R6X/ExjqmAl3P51T+c8B5fWmcBcUr2Ok/5mzk53cU6cG
/kiFHaFpriV1uxPMUgP17VGhi9sVAgMBAAGjggEIMIIBBDAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0lBBYwFAYIKwYBBQUHAwIGCCsGAQUFBwMBMBIGA1UdEwEB/wQIMAYB
Af8CAQAwHQYDVR0OBBYEFBQusxe3WFbLrlAJQOYfr52LFMLGMB8GA1UdIwQYMBaA
FHm0WeZ7tuXkAXOACIjIGlj26ZtuMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEFBQcw
AoYWaHR0cDovL3gxLmkubGVuY3Iub3JnLzAnBgNVHR8EIDAeMBygGqAYhhZodHRw
Oi8veDEuYy5sZW5jci5vcmcvMCIGA1UdIAQbMBkwCAYGZ4EMAQIBMA0GCysGAQQB
gt8TAQEBMA0GCSqGSIb3DQEBCwUAA4ICAQCFyk5HPqP3hUSFvNVneLKYY611TR6W
PTNlclQtgaDqw+34IL9fzLdwALduO/ZelN7kIJ+m74uyA+eitRY8kc607TkC53wl
ikfmZW4/RvTZ8M6UK+5UzhK8jCdLuMGYL6KvzXGRSgi3yLgjewQtCPkIVz6D2QQz
CkcheAmCJ8MqyJu5zlzyZMjAvnnAT45tRAxekrsu94sQ4egdRCnbWSDtY7kh+BIm
lJNXoB1lBMEKIq4QDUOXoRgffuDghje1WrG9ML+Hbisq/yFOGwXD9RiX8F6sw6W4
avAuvDszue5L3sz85K+EC4Y/wFVDNvZo4TYXao6Z0f+lQKc0t8DQYzk1OXVu8rp2
yJMC6alLbBfODALZvYH7n7do1AZls4I9d1P4jnkDrQoxB3UqQ9hVl3LEKQ73xF1O
yK5GhDDX8oVfGKF5u+decIsH4YaTw7mP3GFxJSqv3+0lUFJoi5Lc5da149p90Ids
hCExroL1+7mryIkXPeFM5TgO9r0rvZaBFOvV2z0gp35Z0+L4WPlbuEjN/lxPFin+
HlUjr8gRsI3qfJOQFy/9rKIJR0Y/8Omwt/8oTWgy1mdeHmmjk7j1nYsvC9JSQ6Zv
MldlTTKB3zhThV1+XWYp6rjd5JW1zbVWEkLNxE7GJThEUG3szgBVGP7pSWTUTsqX
nLRbwHOoq7hHwg==
-----END CERTIFICATE-----
)EOF";

// MQTT client objects
WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

// ESP8266 BearSSL certificate trust anchor (must persist beyond setupMQTT())
#if defined(ESP8266)
  static BearSSL::X509List serverTrustAnchor(ROOT_CA_CERT);
#endif

// Heartbeat state
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 60000;  // 60 seconds

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

// Initialize MQTT connection
void setupMQTT() {
  DEBUG_PRINTLN("Initializing MQTT client...");
  
  // Configure TLS for HiveMQ Cloud with certificate validation
  #if defined(ESP32)
    wifiSecureClient.setCACert(ROOT_CA_CERT);
  #elif defined(ESP8266)
    // ESP8266: Use static BearSSL X.509 certificate list (must persist)
    wifiSecureClient.setTrustAnchors(&serverTrustAnchor);
  #endif
  
  // Configure MQTT client
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);  // Increase buffer for JSON messages
  // Keep-alive defaults to 15 seconds (PubSubClient library default)
  
  DEBUG_PRINT("MQTT broker: ");
  DEBUG_PRINT(MQTT_HOST);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(MQTT_PORT);
  DEBUG_PRINTLN("TLS certificate validation enabled");
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
  
  DEBUG_PRINT("Connecting to MQTT broker...");
  
  // Generate unique client ID
  String clientId = "SkiClock-" + getDeviceID();
  
  // Attempt connection
  if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
    DEBUG_PRINTLN("connected!");
    
    // Subscribe to version update topic
    if (mqttClient.subscribe(MQTT_TOPIC_VERSION_UPDATES)) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(MQTT_TOPIC_VERSION_UPDATES);
    } else {
      DEBUG_PRINTLN("Failed to subscribe to version updates topic");
    }
    
    return true;
  } else {
    DEBUG_PRINT("failed, rc=");
    DEBUG_PRINTLN(mqttClient.state());
    return false;
  }
}

// Publish heartbeat message
void publishHeartbeat(const char* firmwareVersion) {
  if (!mqttClient.connected()) {
    return;
  }
  
  // Build JSON heartbeat payload
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"uptime\":%lu,\"rssi\":%d,\"free_heap\":%u}",
    getDeviceID().c_str(),
    getBoardType().c_str(),
    firmwareVersion,
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

// Main MQTT loop (call frequently from main loop)
void loopMQTT(const char* firmwareVersion) {
  // Maintain MQTT connection
  if (!mqttClient.connected()) {
    // Attempt reconnection every 5 seconds
    static unsigned long lastReconnectAttempt = 0;
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      connectMQTT();
    }
  } else {
    // Process incoming messages
    mqttClient.loop();
    
    // Publish heartbeat at regular intervals
    unsigned long now = millis();
    if (now - lastHeartbeat > HEARTBEAT_INTERVAL) {
      lastHeartbeat = now;
      publishHeartbeat(firmwareVersion);
    }
  }
}

#endif
