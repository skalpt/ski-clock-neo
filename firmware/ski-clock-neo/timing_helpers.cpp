#include "timing_helpers.h"

TimerTaskManager& TimerTaskManager::getInstance() {
  static TimerTaskManager instance;
  return instance;
}

TimerTaskManager::TimerTaskManager() : timerCount(0) {
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

TimerConfig* TimerTaskManager::findTimer(const char* name) {
  for (uint8_t i = 0; i < timerCount; i++) {
    if (timers[i].name != nullptr && strcmp(timers[i].name, name) == 0) {
      return &timers[i];
    }
  }
  return nullptr;
}

#if defined(ESP32)
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

bool TimerTaskManager::createTimer(const char* name, uint32_t intervalMs, TimerCallback callback, uint16_t stackSize) {
  if (timerCount >= MAX_TIMERS) {
    DEBUG_PRINTLN("ERROR: Maximum timer count reached");
    return false;
  }
  
  if (callback == nullptr) {
    DEBUG_PRINTLN("ERROR: Timer callback is null");
    return false;
  }
  
  TimerConfig* config = &timers[timerCount];
  config->name = name;
  config->intervalMs = intervalMs;
  config->callback = callback;
  config->stackSize = stackSize;
  config->isActive = true;
  config->isOneShot = false;
  
  #if defined(ESP32)
    config->espTicker = nullptr;
    #if defined(CONFIG_IDF_TARGET_ESP32C3)
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
    config->ticker = new TickTwo(callback, intervalMs, 0, MILLIS);
    config->ticker->start();
    DEBUG_PRINT("Timer created (ESP8266 TickTwo): ");
  #endif
  
  DEBUG_PRINT(name);
  DEBUG_PRINT(" @ ");
  DEBUG_PRINT(intervalMs);
  DEBUG_PRINTLN("ms");
  
  timerCount++;
  return true;
}

bool TimerTaskManager::createOneShotTimer(const char* name, uint32_t intervalMs, TimerCallback callback) {
  if (timerCount >= MAX_TIMERS) {
    DEBUG_PRINTLN("ERROR: Maximum timer count reached");
    return false;
  }
  
  if (callback == nullptr) {
    DEBUG_PRINTLN("ERROR: Timer callback is null");
    return false;
  }
  
  TimerConfig* config = &timers[timerCount];
  config->name = name;
  config->intervalMs = intervalMs;
  config->callback = callback;
  config->stackSize = 0;
  config->isActive = false;
  config->isOneShot = true;
  
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
  
  timerCount++;
  return true;
}

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

#if defined(ESP32)
TaskHandle_t TimerTaskManager::createNotificationTask(const char* name, TaskFunction taskFn, uint16_t stackSize, uint8_t priority) {
  if (taskFn == nullptr) {
    DEBUG_PRINTLN("ERROR: Task function is null");
    return NULL;
  }
  
  TaskHandle_t taskHandle = NULL;
  
  #if defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3 (single-core RISC-V): Run on Core 0
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
    // ESP32/ESP32-S3 (dual-core Xtensa): Pin to Core 1 (APP_CPU)
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

bool TimerTaskManager::notifyTask(TaskHandle_t taskHandle) {
  if (taskHandle == NULL) {
    return false;
  }
  xTaskNotifyGive(taskHandle);
  return true;
}
#endif

void TimerTaskManager::updateAll() {
  #if defined(ESP8266)
    for (uint8_t i = 0; i < timerCount; i++) {
      if (timers[i].isActive && timers[i].ticker != nullptr) {
        timers[i].ticker->update();
      }
    }
  #endif
}

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
