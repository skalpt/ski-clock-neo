#include "mqtt_client.h"
#include "ota_update.h"
#include "../core/led_indicator.h"
#include "../core/event_log.h"

const uint16_t MQTT_PORT = 8883;

static char mqttProductName[32] = "generic";
static char topicBase[64] = "norrtek-iot/generic";
static SnapshotPayloadCallback snapshotPayloadCallback = nullptr;

void setSnapshotPayloadCallback(SnapshotPayloadCallback callback) {
  snapshotPayloadCallback = callback;
}

const char* MQTT_TOPIC_HEARTBEAT = "heartbeat";
const char* MQTT_TOPIC_VERSION_RESPONSE = "version/response";
const char* MQTT_TOPIC_COMMAND = "command";
const char* MQTT_TOPIC_OTA_START = "ota/start";
const char* MQTT_TOPIC_OTA_PROGRESS = "ota/progress";
const char* MQTT_TOPIC_OTA_COMPLETE = "ota/complete";
const char* MQTT_TOPIC_DISPLAY_SNAPSHOT = "display/snapshot";
const char* MQTT_TOPIC_EVENTS = "event";

const unsigned long HEARTBEAT_INTERVAL = 60000;
const unsigned long DISPLAY_SNAPSHOT_INTERVAL = 3600000;

WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);
Ticker heartbeatTicker;
Ticker displaySnapshotTicker;
bool mqttIsConnected = false;

static const char BASE64_CHARS[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void setMqttProduct(const char* productName) {
  strncpy(mqttProductName, productName, sizeof(mqttProductName) - 1);
  mqttProductName[sizeof(mqttProductName) - 1] = '\0';
  
  snprintf(topicBase, sizeof(topicBase), "norrtek-iot/%s", productName);
  
  DEBUG_PRINT("MQTT product set to: ");
  DEBUG_PRINTLN(productName);
  DEBUG_PRINT("Topic base: ");
  DEBUG_PRINTLN(topicBase);
}

const char* getMqttProduct() {
  return mqttProductName;
}

String buildDeviceTopic(const char* subTopic) {
  String topic = String(topicBase);
  topic += "/";
  topic += subTopic;
  topic += "/";
  topic += getDeviceID();
  return topic;
}

String buildProductTopic(const char* subTopic) {
  String topic = String(topicBase);
  topic += "/";
  topic += subTopic;
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

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  DEBUG_PRINT("MQTT message received on topic: ");
  DEBUG_PRINTLN(topic);
  
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  DEBUG_PRINT("Message: ");
  DEBUG_PRINTLN(message);
  
  String versionResponseTopic = buildDeviceTopic(MQTT_TOPIC_VERSION_RESPONSE);
  if (strcmp(topic, versionResponseTopic.c_str()) == 0) {
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
          
          triggerOTAUpdate(latestVersion);
        }
      }
    } else {
      DEBUG_PRINTLN("Firmware is up to date");
    }
  }
  
  String commandTopic = buildDeviceTopic(MQTT_TOPIC_COMMAND);
  if (strcmp(topic, commandTopic.c_str()) == 0) {
    DEBUG_PRINTLN("Command received!");
    
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

static bool eventPublishWrapper(const char* topic, const char* payload) {
  return publishMqttPayload(topic, payload);
}

static String eventTopicBuilderWrapper(const char* baseTopic) {
  return buildDeviceTopic(baseTopic);
}

void initMQTT() {
  DEBUG_PRINTLN("Initializing MQTT client...");
  
  setEventPublishCallback(eventPublishWrapper, eventTopicBuilderWrapper);
  
  wifiSecureClient.setInsecure();
  
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);
  
  DEBUG_PRINT("MQTT broker: ");
  DEBUG_PRINT(MQTT_HOST);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(MQTT_PORT);
  DEBUG_PRINT("Topic base: ");
  DEBUG_PRINTLN(topicBase);
  DEBUG_PRINTLN("TLS encryption enabled (no cert validation)");

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
}

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
  
  String clientId = String(mqttProductName) + "-" + getDeviceID();
  
  if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
    DEBUG_PRINTLN("MQTT broker connected!");
    setConnectivityState(true, true);
    mqttIsConnected = true;

    logEvent("mqtt_connect");
    setEventLogReady(true);
    flushEventQueue();

    String versionResponseTopic = buildDeviceTopic(MQTT_TOPIC_VERSION_RESPONSE);
    if (mqttClient.subscribe(versionResponseTopic.c_str())) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(versionResponseTopic);
    }
    
    String commandTopic = buildDeviceTopic(MQTT_TOPIC_COMMAND);
    if (mqttClient.subscribe(commandTopic.c_str())) {
      DEBUG_PRINT("Subscribed to topic: ");
      DEBUG_PRINTLN(commandTopic);
    }
    
    heartbeatTicker.detach();
    heartbeatTicker.attach_ms(HEARTBEAT_INTERVAL, publishHeartbeat);
    publishHeartbeat();

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

void updateMQTT() {
  if (mqttIsConnected && !mqttClient.connected()) {
    DEBUG_PRINTLN("MQTT connection lost unexpectedly, cleaning up...");
    disconnectMQTT();
  }
  
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      DEBUG_PRINTLN("Attempting MQTT reconnection...");
      connectMQTT();
    }
  }
  
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
}

void publishHeartbeat() {
  if (!mqttClient.connected()) return;
  
  int32_t rssi = WiFi.RSSI();
  uint32_t freeHeap = ESP.getFreeHeap();
  
  static char payload[384];
  snprintf(payload, sizeof(payload),
    "{\"product\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"uptime\":%lu,\"rssi\":%ld,\"free_heap\":%lu,\"ssid\":\"%s\",\"ip\":\"%s\"}",
    mqttProductName,
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
  
  if (snapshotPayloadCallback == nullptr) {
    DEBUG_PRINTLN("Snapshot callback not set, skipping snapshot");
    return;
  }
  
  DEBUG_PRINTLN("Publishing display snapshot...");
  
  String payload = snapshotPayloadCallback();
  
  if (payload.length() == 0) {
    DEBUG_PRINTLN("Empty snapshot payload, skipping");
    return;
  }
  
  if (payload.length() > 2000) {
    DEBUG_PRINT("Payload too large: ");
    DEBUG_PRINT(payload.length());
    DEBUG_PRINTLN(" bytes (max 2000)");
    return;
  }
  
  if (publishMqttPayload(buildDeviceTopic(MQTT_TOPIC_DISPLAY_SNAPSHOT), payload)) {
    DEBUG_PRINT("Display snapshot size: ");
    DEBUG_PRINT(payload.length());
    DEBUG_PRINTLN(" bytes");
  }
}

void handleRestartCommand() {
  DEBUG_PRINTLN("Restart command received, rebooting in 2 seconds...");
  delay(2000);
  ESP.restart();
}

void handleRollbackCommand(String message) {
  DEBUG_PRINTLN("Rollback command processing...");
  
#if defined(ESP32)
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
