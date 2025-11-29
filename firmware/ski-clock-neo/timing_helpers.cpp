// ============================================================================
// timing_helpers.cpp - Unified timer abstraction for ESP32 and ESP8266
// ============================================================================
// This library provides a consistent timer API across platforms:
// - ESP32: Uses FreeRTOS tasks with vTaskDelayUntil for periodic timers
// - ESP8266: Uses TickTwo library for non-blocking timer callbacks
// 
// Features:
// - Periodic timers via createTimer()
// - One-shot timers via createOneShotTimer() and triggerTimer()
// - Notification-based tasks via createNotificationTask() (ESP32 only)
// - Zero runtime overhead (inline wrappers in header)
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "timing_helpers.h"

// ============================================================================
// SINGLETON INSTANCE
// ============================================================================

// Get singleton instance of TimerTaskManager
TimerTaskManager& TimerTaskManager::getInstance() {
  static TimerTaskManager instance;
  return instance;
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================

TimerTaskManager::TimerTaskManager() : timerCount(0) {
  // Initialize all timer slots to inactive
  for (uint8_t i = 0; i < MAX_TIMERS; i++) {
    timers[i].isActive = false;
    timers[i].isOneShot = false;
    timers[i].name = nullptr;
    timers[i].callback = nullptr;
    #if defined(ESP32)
      timers[i].taskHandle = nullptr;
      timers[i].espTicker = nullptr;
    #elif defined(ESP8266)
      timers[i].ticker = nullptr;
    #endif
  }
}

// ============================================================================
// TIMER LOOKUP
// ============================================================================

// Find timer by name (returns nullptr if not found)
TimerConfig* TimerTaskManager::findTimer(const char* name) {
  for (uint8_t i = 0; i < timerCount; i++) {
    if (timers[i].name != nullptr && strcmp(timers[i].name, name) == 0) {
      return &timers[i];
    }
  }
  return nullptr;
}

// ============================================================================
// ESP32 TASK WRAPPER
// ============================================================================

#if defined(ESP32)
// FreeRTOS task wrapper for periodic timers
void TimerTaskManager::taskWrapper(void* parameter) {
  TimerConfig* config = static_cast<TimerConfig*>(parameter);
  
  DEBUG_PRINT("FreeRTOS task started: ");
  DEBUG_PRINTLN(config->name);
  
  TickType_t lastWakeTime = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(config->intervalMs);
  
  for(;;) {
    vTaskDelayUntil(&lastWakeTime, interval);
    if (config->callback != nullptr) {
      config->callback();
    }
  }
}
#endif

// ============================================================================
// PERIODIC TIMER CREATION
// ============================================================================

// Helper to fully clean up a timer config (free resources and reset all fields)
void TimerTaskManager::cleanupTimerConfig(TimerConfig* config) {
  if (config == nullptr) return;
  
  #if defined(ESP32)
    if (config->taskHandle != nullptr) {
      vTaskDelete(config->taskHandle);
      config->taskHandle = nullptr;
    }
    if (config->espTicker != nullptr) {
      config->espTicker->detach();
      delete config->espTicker;
      config->espTicker = nullptr;
    }
  #elif defined(ESP8266)
    if (config->ticker != nullptr) {
      config->ticker->stop();
      delete config->ticker;
      config->ticker = nullptr;
    }
  #endif
  
  config->isActive = false;
}

// Create a periodic timer that fires at regular intervals
bool TimerTaskManager::createTimer(const char* name, uint32_t intervalMs, TimerCallback callback, uint16_t stackSize) {
  if (callback == nullptr) {
    DEBUG_PRINTLN("ERROR: Timer callback is null");
    return false;
  }
  
  // Check if timer with this name already exists - reuse the slot
  TimerConfig* config = findTimer(name);
  if (config != nullptr) {
    // Fully clean up existing timer first
    cleanupTimerConfig(config);
    DEBUG_PRINT("Reusing timer slot: ");
    DEBUG_PRINTLN(name);
  } else {
    // Need a new slot
    if (timerCount >= MAX_TIMERS) {
      DEBUG_PRINTLN("ERROR: Maximum timer count reached");
      return false;
    }
    config = &timers[timerCount];
    timerCount++;
  }
  
  // Initialize all fields for periodic timer
  config->name = name;
  config->intervalMs = intervalMs;
  config->callback = callback;
  config->stackSize = stackSize;
  config->isActive = true;
  config->isOneShot = false;
  
  // Platform-specific timer creation
  #if defined(ESP32)
    config->espTicker = nullptr;
    config->taskHandle = nullptr;
    #if defined(CONFIG_IDF_TARGET_ESP32C3)
      // ESP32-C3: Single-core RISC-V, use xTaskCreate
      xTaskCreate(
        taskWrapper,
        name,
        stackSize,
        config,
        2,
        &config->taskHandle
      );
      DEBUG_PRINT("Timer created (ESP32-C3 FreeRTOS): ");
    #else
      // ESP32/ESP32-S3: Dual-core Xtensa, pin to Core 1
      xTaskCreatePinnedToCore(
        taskWrapper,
        name,
        stackSize,
        config,
        2,
        &config->taskHandle,
        1
      );
      DEBUG_PRINT("Timer created (ESP32 FreeRTOS, Core 1): ");
    #endif
  #elif defined(ESP8266)
    // ESP8266: Use TickTwo library
    config->ticker = new TickTwo(callback, intervalMs, 0, MILLIS);
    config->ticker->start();
    DEBUG_PRINT("Timer created (ESP8266 TickTwo): ");
  #endif
  
  DEBUG_PRINT(name);
  DEBUG_PRINT(" @ ");
  DEBUG_PRINT(intervalMs);
  DEBUG_PRINTLN("ms");
  
  return true;
}

// ============================================================================
// ONE-SHOT TIMER CREATION
// ============================================================================

// Create a one-shot timer (dormant until triggered)
bool TimerTaskManager::createOneShotTimer(const char* name, uint32_t intervalMs, TimerCallback callback) {
  if (callback == nullptr) {
    DEBUG_PRINTLN("ERROR: Timer callback is null");
    return false;
  }
  
  // Check if timer with this name already exists - reuse the slot
  TimerConfig* config = findTimer(name);
  if (config != nullptr) {
    // Fully clean up existing timer first
    cleanupTimerConfig(config);
    DEBUG_PRINT("Reusing one-shot timer slot: ");
    DEBUG_PRINTLN(name);
  } else {
    // Need a new slot
    if (timerCount >= MAX_TIMERS) {
      DEBUG_PRINTLN("ERROR: Maximum timer count reached");
      return false;
    }
    config = &timers[timerCount];
    timerCount++;
  }
  
  // Initialize all fields for one-shot timer
  config->name = name;
  config->intervalMs = intervalMs;
  config->callback = callback;
  config->stackSize = 0;
  config->isActive = false;  // Dormant until triggered
  config->isOneShot = true;
  
  // Platform-specific timer setup
  #if defined(ESP32)
    config->taskHandle = nullptr;
    config->espTicker = new Ticker();
  #elif defined(ESP8266)
    config->ticker = new TickTwo(callback, intervalMs, 1, MILLIS);
  #endif
  
  DEBUG_PRINT("One-shot timer registered: ");
  DEBUG_PRINT(name);
  DEBUG_PRINT(" @ ");
  DEBUG_PRINT(intervalMs);
  DEBUG_PRINTLN("ms (dormant)");
  
  return true;
}

// ============================================================================
// ONE-SHOT TIMER TRIGGER
// ============================================================================

// Trigger a previously registered one-shot timer
bool TimerTaskManager::triggerTimer(const char* name) {
  TimerConfig* config = findTimer(name);
  if (config == nullptr) {
    DEBUG_PRINT("ERROR: Timer not found: ");
    DEBUG_PRINTLN(name);
    return false;
  }
  
  if (!config->isOneShot) {
    DEBUG_PRINT("ERROR: Not a one-shot timer: ");
    DEBUG_PRINTLN(name);
    return false;
  }
  
  // Platform-specific trigger
  #if defined(ESP32)
    if (config->espTicker != nullptr) {
      config->espTicker->once_ms(config->intervalMs, config->callback);
    }
  #elif defined(ESP8266)
    if (config->ticker != nullptr) {
      config->ticker->start();
    }
  #endif
  
  config->isActive = true;
  DEBUG_PRINT("One-shot timer triggered: ");
  DEBUG_PRINTLN(name);
  return true;
}

// ============================================================================
// NOTIFICATION-BASED TASKS (ESP32 only)
// ============================================================================

#if defined(ESP32)
// Create a task that waits for notifications (event-driven wakeup)
TaskHandle_t TimerTaskManager::createNotificationTask(const char* name, TaskFunction taskFn, uint16_t stackSize, uint8_t priority) {
  if (taskFn == nullptr) {
    DEBUG_PRINTLN("ERROR: Task function is null");
    return NULL;
  }
  
  TaskHandle_t taskHandle = NULL;
  
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3: Single-core RISC-V
    xTaskCreate(
      taskFn,
      name,
      stackSize,
      NULL,
      priority,
      &taskHandle
    );
    DEBUG_PRINT("Notification task created (ESP32-C3): ");
  #else
    // ESP32/ESP32-S3: Dual-core Xtensa, pin to Core 1
    xTaskCreatePinnedToCore(
      taskFn,
      name,
      stackSize,
      NULL,
      priority,
      &taskHandle,
      1
    );
    DEBUG_PRINT("Notification task created (ESP32, Core 1): ");
  #endif
  
  DEBUG_PRINTLN(name);
  return taskHandle;
}

// Send notification to wake a task
bool TimerTaskManager::notifyTask(TaskHandle_t taskHandle) {
  if (taskHandle == NULL) {
    DEBUG_PRINTLN("ERROR: notifyTask called with NULL handle");
    return false;
  }
  xTaskNotifyGive(taskHandle);
  return true;
}
#endif

// ============================================================================
// ESP8266 UPDATE LOOP
// ============================================================================

// Update all TickTwo timers (ESP8266 only, call from main loop)
void TimerTaskManager::updateAll() {
  #if defined(ESP8266)
    for (uint8_t i = 0; i < timerCount; i++) {
      if (timers[i].isActive && timers[i].ticker != nullptr) {
        timers[i].ticker->update();
      }
    }
  #endif
}

// ============================================================================
// TIMER CONTROL
// ============================================================================

// Stop a specific timer by name
void TimerTaskManager::stopTimer(const char* name) {
  TimerConfig* config = findTimer(name);
  if (config == nullptr || !config->isActive) {
    return;
  }
  
  #if defined(ESP32)
    if (config->taskHandle != nullptr) {
      vTaskDelete(config->taskHandle);
      config->taskHandle = nullptr;
    }
    if (config->espTicker != nullptr) {
      config->espTicker->detach();
    }
  #elif defined(ESP8266)
    if (config->ticker != nullptr) {
      config->ticker->stop();
    }
  #endif
  
  config->isActive = false;
  DEBUG_PRINT("Timer stopped: ");
  DEBUG_PRINTLN(name);
}

// Stop all timers
void TimerTaskManager::stopAll() {
  for (uint8_t i = 0; i < timerCount; i++) {
    if (timers[i].isActive) {
      #if defined(ESP32)
        if (timers[i].taskHandle != nullptr) {
          vTaskDelete(timers[i].taskHandle);
          timers[i].taskHandle = nullptr;
        }
        if (timers[i].espTicker != nullptr) {
          timers[i].espTicker->detach();
        }
      #elif defined(ESP8266)
        if (timers[i].ticker != nullptr) {
          timers[i].ticker->stop();
        }
      #endif
      timers[i].isActive = false;
    }
  }
  DEBUG_PRINTLN("All timers stopped");
}
