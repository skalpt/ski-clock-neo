#ifndef TIMER_TASK_H
#define TIMER_TASK_H

#include <Arduino.h>
#include "debug.h"

#if defined(ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <Ticker.h>
#elif defined(ESP8266)
  #include <TickTwo.h>
#endif

typedef void (*TimerCallback)();

#define MAX_TIMERS 10

struct TimerConfig {
  const char* name;
  uint32_t intervalMs;
  TimerCallback callback;
  uint16_t stackSize;
  bool isActive;
  bool isOneShot;
  #if defined(ESP32)
    TaskHandle_t taskHandle;
    Ticker* espTicker;
  #elif defined(ESP8266)
    TickTwo* ticker;
  #endif
};

// Task function type for notification-based tasks (ESP32 only)
typedef void (*TaskFunction)(void* parameter);

class TimerTaskManager {
public:
  static TimerTaskManager& getInstance();
  
  bool createTimer(const char* name, uint32_t intervalMs, TimerCallback callback, uint16_t stackSize = 2048);
  
  bool createOneShotTimer(const char* name, uint32_t intervalMs, TimerCallback callback);
  
  bool triggerTimer(const char* name);
  
  #if defined(ESP32)
    // Create a notification-based task (ESP32 only)
    // Task blocks on ulTaskNotifyTake() and wakes when notifyTask() is called
    // Returns task handle for external notification, or NULL on failure
    TaskHandle_t createNotificationTask(const char* name, TaskFunction taskFn, uint16_t stackSize = 2048, uint8_t priority = 2);
    
    // Notify a task to wake up (use with createNotificationTask)
    bool notifyTask(TaskHandle_t taskHandle);
  #endif
  
  void updateAll();
  
  void stopTimer(const char* name);
  
  void stopAll();

private:
  TimerTaskManager();
  TimerConfig timers[MAX_TIMERS];
  uint8_t timerCount;
  
  TimerConfig* findTimer(const char* name);
  
  #if defined(ESP32)
    static void taskWrapper(void* parameter);
  #endif
};

inline bool createTimer(const char* name, uint32_t intervalMs, TimerCallback callback, uint16_t stackSize = 2048) {
  return TimerTaskManager::getInstance().createTimer(name, intervalMs, callback, stackSize);
}

inline bool createOneShotTimer(const char* name, uint32_t intervalMs, TimerCallback callback) {
  return TimerTaskManager::getInstance().createOneShotTimer(name, intervalMs, callback);
}

inline bool triggerTimer(const char* name) {
  return TimerTaskManager::getInstance().triggerTimer(name);
}

#if defined(ESP32)
inline TaskHandle_t createNotificationTask(const char* name, TaskFunction taskFn, uint16_t stackSize = 2048, uint8_t priority = 2) {
  return TimerTaskManager::getInstance().createNotificationTask(name, taskFn, stackSize, priority);
}

inline bool notifyTask(TaskHandle_t taskHandle) {
  return TimerTaskManager::getInstance().notifyTask(taskHandle);
}
#endif

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
