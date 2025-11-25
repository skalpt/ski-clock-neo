#include "event_log.h"
#include "mqtt_client.h"
#include "device_info.h"
#include "debug.h"

static EventEntry eventQueue[EVENT_QUEUE_SIZE];
static uint8_t queueHead = 0;
static uint8_t queueTail = 0;
static uint8_t queueCount = 0;
static bool eventLogReady = false;

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

void setEventLogReady(bool ready) {
    eventLogReady = ready;
}

void logEvent(const char* type, const char* dataJson) {
    if (!type) return;
    
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
        DEBUG_PRINTLN("Event queue overflow - oldest event dropped");
    }
    
    DEBUG_PRINT("Event queued: ");
    DEBUG_PRINT(type);
    if (dataJson) {
        DEBUG_PRINT(" ");
        DEBUG_PRINT(dataJson);
    }
    DEBUG_PRINT(" (queue: ");
    DEBUG_PRINT(queueCount);
    DEBUG_PRINTLN(")");
    
    if (eventLogReady && mqttIsConnected) {
        flushEventQueue();
    }
}

void flushEventQueue() {
    if (!mqttIsConnected || queueCount == 0) return;
    
    String deviceId = getDeviceID();
    String topic = String("skiclock/events/") + deviceId;
    
    uint32_t now = millis();
    int flushed = 0;
    
    while (queueCount > 0) {
        EventEntry& entry = eventQueue[queueHead];
        
        if (entry.valid) {
            uint32_t offset_ms = now - entry.timestamp_ms;
            
            String payload = "{\"type\":\"";
            payload += entry.type;
            payload += "\"";
            
            if (entry.data[0] != '\0') {
                payload += ",\"data\":";
                payload += entry.data;
            }
            
            payload += ",\"offset_ms\":";
            payload += offset_ms;
            payload += "}";
            
            if (mqttClient.publish(topic.c_str(), payload.c_str())) {
                DEBUG_PRINT("Event flushed: ");
                DEBUG_PRINT(entry.type);
                DEBUG_PRINT(" (offset: ");
                DEBUG_PRINT(offset_ms);
                DEBUG_PRINTLN("ms)");
                flushed++;
            } else {
                DEBUG_PRINT("Failed to publish event: ");
                DEBUG_PRINTLN(entry.type);
                break;
            }
            
            entry.valid = false;
        }
        
        queueHead = (queueHead + 1) % EVENT_QUEUE_SIZE;
        queueCount--;
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
