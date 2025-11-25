#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <Arduino.h>

#define EVENT_TYPE_MAX_LEN 16
#define EVENT_DATA_MAX_LEN 64
#define EVENT_QUEUE_SIZE 50

struct EventEntry {
    uint32_t timestamp_ms;
    char type[EVENT_TYPE_MAX_LEN];
    char data[EVENT_DATA_MAX_LEN];
    bool valid;
};

void initEventLog();
void logEvent(const char* type, const char* dataJson = nullptr);
void logBootEvent();
void flushEventQueue();
bool hasQueuedEvents();
int getQueuedEventCount();
void setEventLogReady(bool ready);

#endif
