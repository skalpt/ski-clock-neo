#include "timer_task.h"

TimerTaskManager& TimerTaskManager::getInstance() {
  static TimerTaskManager instance;
  return instance;
}

TimerTaskManager::TimerTaskManager() : timerCount(0) {
  for (uint8_t i = 0; i < MAX_TIMERS; i++) {
    timers[i].isActive = false;
    timers[i].name = nullptr;
    timers[i].callback = nullptr;
    #if defined(ESP32)
      timers[i].taskHandle = nullptr;
    #elif defined(ESP8266)
      timers[i].ticker = nullptr;
    #endif
  }
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
  
  #if defined(ESP32)
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
  for (uint8_t i = 0; i < timerCount; i++) {
    if (timers[i].isActive && strcmp(timers[i].name, name) == 0) {
      #if defined(ESP32)
        if (timers[i].taskHandle != nullptr) {
          vTaskDelete(timers[i].taskHandle);
          timers[i].taskHandle = nullptr;
        }
      #elif defined(ESP8266)
        if (timers[i].ticker != nullptr) {
          timers[i].ticker->stop();
          delete timers[i].ticker;
          timers[i].ticker = nullptr;
        }
      #endif
      timers[i].isActive = false;
      DEBUG_PRINT("Timer stopped: ");
      DEBUG_PRINTLN(name);
      return;
    }
  }
}

void TimerTaskManager::stopAll() {
  for (uint8_t i = 0; i < timerCount; i++) {
    if (timers[i].isActive) {
      #if defined(ESP32)
        if (timers[i].taskHandle != nullptr) {
          vTaskDelete(timers[i].taskHandle);
          timers[i].taskHandle = nullptr;
        }
      #elif defined(ESP8266)
        if (timers[i].ticker != nullptr) {
          timers[i].ticker->stop();
          delete timers[i].ticker;
          timers[i].ticker = nullptr;
        }
      #endif
      timers[i].isActive = false;
    }
  }
  DEBUG_PRINTLN("All timers stopped");
}
