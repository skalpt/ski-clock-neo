#include "event_log.h"
#include "../connectivity/mqtt_client.h"
#include "device_info.h"
#include "debug.h"

#if defined(ESP32)
  #include "esp_system.h"
#endif

#if defined(ESP32)
  static portMUX_TYPE eventQueueMux = portMUX_INITIALIZER_UNLOCKED;
  #define EVENT_ENTER_CRITICAL() portENTER_CRITICAL(&eventQueueMux)
  #define EVENT_EXIT_CRITICAL() portEXIT_CRITICAL(&eventQueueMux)
#else
  #define EVENT_ENTER_CRITICAL() noInterrupts()
  #define EVENT_EXIT_CRITICAL() interrupts()
#endif

static EventEntry eventQueue[EVENT_QUEUE_SIZE];
static volatile uint8_t queueHead = 0;
static volatile uint8_t queueTail = 0;
static volatile uint8_t queueCount = 0;
static bool eventLogReady = false;

static const char* getResetReason() {
#if defined(ESP32)
  esp_reset_reason_t reason = esp_reset_reason();
  switch (reason) {
    case ESP_RST_POWERON:   return "power_on";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "crash";
    case ESP_RST_INT_WDT:   return "watchdog_int";
    case ESP_RST_TASK_WDT:  return "watchdog_task";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
  }
#elif defined(ESP8266)
  rst_info* info = ESP.getResetInfoPtr();
  switch (info->reason) {
    case REASON_DEFAULT_RST:      return "power_on";
    case REASON_WDT_RST:          return "watchdog";
    case REASON_EXCEPTION_RST:    return "crash";
    case REASON_SOFT_WDT_RST:     return "soft_watchdog";
    case REASON_SOFT_RESTART:     return "software";
    case REASON_DEEP_SLEEP_AWAKE: return "deep_sleep";
    case REASON_EXT_SYS_RST:      return "external";
    default:                      return "unknown";
  }
#else
  return "unknown";
#endif
}

void initEventLog() {
  for (int i = 0; i < EVENT_QUEUE_SIZE; i++) {
    eventQueue[i].valid = false;
  }
  queueHead = 0;
  queueTail = 0;
  queueCount = 0;
  eventLogReady = false;
  DEBUG_PRINTLN("Event log initialized");
}

void logBootEvent() {
  String bootData = "{\"reason\":\"";
  bootData += getResetReason();
  bootData += "\",\"version\":\"";
  bootData += FIRMWARE_VERSION;
  bootData += "\"}";
  logEvent("boot", bootData.c_str());
  DEBUG_PRINT("Boot event logged: ");
  DEBUG_PRINTLN(bootData);
}

void setEventLogReady(bool ready) {
  eventLogReady = ready;
}

void logEvent(const char* type, const char* dataJson) {
  if (!type) return;
  
  EVENT_ENTER_CRITICAL();
  
  EventEntry& entry = eventQueue[queueTail];
  entry.timestamp_ms = millis();
  strncpy(entry.type, type, EVENT_TYPE_MAX_LEN - 1);
  entry.type[EVENT_TYPE_MAX_LEN - 1] = '\0';
  
  if (dataJson) {
    strncpy(entry.data, dataJson, EVENT_DATA_MAX_LEN - 1);
    entry.data[EVENT_DATA_MAX_LEN - 1] = '\0';
  } else {
    entry.data[0] = '\0';
  }
  entry.valid = true;
  
  queueTail = (queueTail + 1) % EVENT_QUEUE_SIZE;
  if (queueCount < EVENT_QUEUE_SIZE) {
    queueCount++;
  } else {
    queueHead = (queueHead + 1) % EVENT_QUEUE_SIZE;
  }
  
  uint8_t currentCount = queueCount;
  bool shouldFlush = eventLogReady && mqttIsConnected;
  
  EVENT_EXIT_CRITICAL();
  
  DEBUG_PRINT("Event queued: ");
  DEBUG_PRINT(type);
  if (dataJson) {
    DEBUG_PRINT(" ");
    DEBUG_PRINT(dataJson);
  }
  DEBUG_PRINT(" (queue: ");
  DEBUG_PRINT(currentCount);
  DEBUG_PRINTLN(")");
  
  if (shouldFlush) {
    flushEventQueue();
  }
}

void flushEventQueue() {
  if (!mqttIsConnected) return;
  
  EVENT_ENTER_CRITICAL();
  if (queueCount == 0) {
    EVENT_EXIT_CRITICAL();
    return;
  }
  EVENT_EXIT_CRITICAL();
  
  String topic = buildDeviceTopic(MQTT_TOPIC_EVENTS);
  uint32_t now = millis();
  int flushed = 0;
  
  static char payload[256];
  
  while (true) {
    EVENT_ENTER_CRITICAL();
    if (queueCount == 0) {
      EVENT_EXIT_CRITICAL();
      break;
    }
    
    EventEntry& entry = eventQueue[queueHead];
    uint32_t timestamp = entry.timestamp_ms;
    char type[EVENT_TYPE_MAX_LEN];
    char data[EVENT_DATA_MAX_LEN];
    bool valid = entry.valid;
    
    strncpy(type, entry.type, EVENT_TYPE_MAX_LEN);
    strncpy(data, entry.data, EVENT_DATA_MAX_LEN);
    
    entry.valid = false;
    queueHead = (queueHead + 1) % EVENT_QUEUE_SIZE;
    queueCount--;
    
    EVENT_EXIT_CRITICAL();
    
    if (valid) {
      uint32_t offset_ms = now - timestamp;
      
      if (data[0] != '\0') {
        snprintf(payload, sizeof(payload),
          "{\"type\":\"%s\",\"data\":%s,\"offset_ms\":%lu}",
          type, data, offset_ms);
      } else {
        snprintf(payload, sizeof(payload),
          "{\"type\":\"%s\",\"offset_ms\":%lu}",
          type, offset_ms);
      }
      
      if (publishMqttPayload(topic, payload)) {
        flushed++;
      } else {
        break;
      }
    }
  }
  
  if (flushed > 0) {
    DEBUG_PRINT("Flushed ");
    DEBUG_PRINT(flushed);
    DEBUG_PRINTLN(" events from queue");
  }
}

bool hasQueuedEvents() {
  return queueCount > 0;
}

int getQueuedEventCount() {
  return queueCount;
}
