#ifndef TIMER_TASK_H
#define TIMER_TASK_H

#include <Arduino.h>
#include "debug.h"

#if defined(ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
#elif defined(ESP8266)
  #include <TickTwo.h>
#endif

typedef void (*TimerCallback)();

#define MAX_TIMERS 8

struct TimerConfig {
  const char* name;
  uint32_t intervalMs;
  TimerCallback callback;
  uint16_t stackSize;
  bool isActive;
  #if defined(ESP32)
    TaskHandle_t taskHandle;
  #elif defined(ESP8266)
    TickTwo* ticker;
  #endif
};

class TimerTaskManager {
public:
  static TimerTaskManager& getInstance();
  
  bool createTimer(const char* name, uint32_t intervalMs, TimerCallback callback, uint16_t stackSize = 2048);
  
  void updateAll();
  
  void stopTimer(const char* name);
  
  void stopAll();

private:
  TimerTaskManager();
  TimerConfig timers[MAX_TIMERS];
  uint8_t timerCount;
  
  #if defined(ESP32)
    static void taskWrapper(void* parameter);
  #endif
};

inline bool createTimer(const char* name, uint32_t intervalMs, TimerCallback callback, uint16_t stackSize = 2048) {
  return TimerTaskManager::getInstance().createTimer(name, intervalMs, callback, stackSize);
}

inline void updateTimers() {
  TimerTaskManager::getInstance().updateAll();
}

inline void stopTimer(const char* name) {
  TimerTaskManager::getInstance().stopTimer(name);
}

inline void stopAllTimers() {
  TimerTaskManager::getInstance().stopAll();
}

#endif
