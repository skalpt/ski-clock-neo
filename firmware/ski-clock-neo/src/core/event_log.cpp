// ============================================================================
// event_log.cpp - Event ring buffer with MQTT publishing
// ============================================================================
// This library provides event logging with automatic MQTT publishing:
// - Ring buffer stores events when MQTT is disconnected
// - Events are flushed to MQTT when connection is established
// - Thread-safe access using critical sections
// - Includes reset reason detection for boot events
// - Timestamp handling: waits for RTC/NTP sync (max 60s) before flushing
//   - If time syncs within 60s: uses Unix timestamp
//   - If timeout: uses offset_ms for server-side time calculation
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "event_log.h"                   // This file's header
#include "../connectivity/mqtt_client.h" // For MQTT publishing
#include "../data/data_time.h"           // For time sync and timestamp functions
#include "device_info.h"                 // For device ID and version info
#include "debug.h"                       // For debug logging

#if defined(ESP32)
  #include "esp_system.h"
#endif

// ============================================================================
// PLATFORM-SPECIFIC MACROS
// ============================================================================

#if defined(ESP32)
  static portMUX_TYPE eventQueueMux = portMUX_INITIALIZER_UNLOCKED;
  #define EVENT_ENTER_CRITICAL() portENTER_CRITICAL(&eventQueueMux)
  #define EVENT_EXIT_CRITICAL() portEXIT_CRITICAL(&eventQueueMux)
#else
  #define EVENT_ENTER_CRITICAL() noInterrupts()
  #define EVENT_EXIT_CRITICAL() interrupts()
#endif

// ============================================================================
// CONSTANTS
// ============================================================================

// Maximum time to wait for RTC/NTP sync before using offset fallback
static const unsigned long TIME_SYNC_TIMEOUT_MS = 60000;  // 60 seconds

// ============================================================================
// STATE VARIABLES
// ============================================================================

static EventEntry eventQueue[EVENT_QUEUE_SIZE];
static volatile uint8_t queueHead = 0;
static volatile uint8_t queueTail = 0;
static volatile uint8_t queueCount = 0;
static bool eventLogReady = false;
static uint32_t bootMillis = 0;  // millis() at boot, for timeout calculation

// ============================================================================
// RESET REASON DETECTION
// ============================================================================

// Get human-readable reset reason
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

// ============================================================================
// INITIALIZATION
// ============================================================================

void initEventLog() {
  // Record boot time for 60-second timeout calculation
  bootMillis = millis();
  
  // Clear event queue
  for (int i = 0; i < EVENT_QUEUE_SIZE; i++) {
    eventQueue[i].valid = false;
  }
  queueHead = 0;
  queueTail = 0;
  queueCount = 0;
  eventLogReady = false;
  DEBUG_PRINTLN("Event log initialized");
}

// Log boot event with reset reason and firmware version
void logBootEvent() {
  String bootData = "{\"reason\":\"";
  bootData += getResetReason();
  bootData += "\",\"version\":\"";
  bootData += FIRMWARE_VERSION;
  bootData += "\"}";
  logEvent("boot", bootData.c_str());
}

// ============================================================================
// EVENT LOGGING
// ============================================================================

// Set event log ready state (called when MQTT connects/disconnects)
void setEventLogReady(bool ready) {
  eventLogReady = ready;
}

// Log an event to the ring buffer (thread-safe)
void logEvent(const char* type, const char* dataJson) {
  if (!type) return;
  
  EVENT_ENTER_CRITICAL();
  
  // Store event in ring buffer
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
  
  // Advance tail pointer
  queueTail = (queueTail + 1) % EVENT_QUEUE_SIZE;
  if (queueCount < EVENT_QUEUE_SIZE) {
    queueCount++;
  } else {
    // Buffer full, overwrite oldest entry
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
  
  // Flush if MQTT is ready
  if (shouldFlush) {
    flushEventQueue();
  }
}

// ============================================================================
// EVENT FLUSHING
// ============================================================================

// Check if we should use timestamps (time synced) or offset (fallback)
// Returns true if time is synced OR 60 seconds have passed since boot
static bool shouldFlushEvents() {
  // If time is synced, always allow flushing with timestamps
  if (isTimeSynced()) {
    return true;
  }
  
  // Check if 60-second timeout has elapsed - use offset fallback
  uint32_t elapsed = millis() - bootMillis;
  if (elapsed >= TIME_SYNC_TIMEOUT_MS) {
    return true;
  }
  
  // Still waiting for time sync
  return false;
}

// Flush all queued events to MQTT
void flushEventQueue() {
  if (!mqttIsConnected) return;
  
  // Check if we should flush (time synced or timeout elapsed)
  if (!shouldFlushEvents()) {
    DEBUG_PRINTLN("Waiting for time sync before flushing events...");
    return;
  }
  
  EVENT_ENTER_CRITICAL();
  if (queueCount == 0) {
    EVENT_EXIT_CRITICAL();
    return;
  }
  EVENT_EXIT_CRITICAL();
  
  String topic = buildDeviceTopic(MQTT_TOPIC_EVENTS);
  uint32_t now = millis();
  bool timeAvailable = isTimeSynced();
  int flushed = 0;
  
  // Static buffer for event payloads (reused across iterations)
  static char payload[256];
  
  // Log which mode we're using
  if (timeAvailable) {
    DEBUG_PRINTLN("Flushing events with Unix timestamps");
  } else {
    DEBUG_PRINTLN("Flushing events with offset_ms (time sync timeout)");
  }
  
  // Process all queued events
  while (true) {
    EVENT_ENTER_CRITICAL();
    if (queueCount == 0) {
      EVENT_EXIT_CRITICAL();
      break;
    }
    
    // Copy event data (thread-safe)
    EventEntry& entry = eventQueue[queueHead];
    uint32_t event_millis = entry.timestamp_ms;
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
      if (timeAvailable) {
        // Use Unix timestamp - calculate when event actually occurred
        time_t eventTimestamp = getTimestampForEvent(event_millis);
        
        // Build JSON payload with timestamp
        if (data[0] != '\0') {
          snprintf(payload, sizeof(payload),
            "{\"type\":\"%s\",\"data\":%s,\"timestamp\":%lu}",
            type, data, (unsigned long)eventTimestamp);
        } else {
          snprintf(payload, sizeof(payload),
            "{\"type\":\"%s\",\"timestamp\":%lu}",
            type, (unsigned long)eventTimestamp);
        }
      } else {
        // Fallback: use offset_ms - server calculates receive_time - offset
        uint32_t offset_ms = now - event_millis;
        
        // Build JSON payload with offset
        if (data[0] != '\0') {
          snprintf(payload, sizeof(payload),
            "{\"type\":\"%s\",\"data\":%s,\"offset_ms\":%lu}",
            type, data, offset_ms);
        } else {
          snprintf(payload, sizeof(payload),
            "{\"type\":\"%s\",\"offset_ms\":%lu}",
            type, offset_ms);
        }
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

// ============================================================================
// PUBLIC API
// ============================================================================

// Check if there are queued events
bool hasQueuedEvents() {
  return queueCount > 0;
}

// Get count of queued events
int getQueuedEventCount() {
  return queueCount;
}
