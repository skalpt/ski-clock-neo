#ifndef DATA_TIME_H
#define DATA_TIME_H

#include <Arduino.h>

// Initialize NTP client with Sweden timezone (Europe/Stockholm)
// NTP server: pool.ntp.org
void initTimeData();

// Check if time is synchronized from NTP
bool isTimeSynced();

// Format current time as "hh.mm" (e.g., "09.53", "23.59")
// Returns true if successful, false if time not synced
// Output buffer must be at least 6 bytes (5 chars + null terminator)
bool formatTime(char* output, size_t outputSize);

// Format current date as "dd-mm" (e.g., "01-01", "31-12")
// Returns true if successful, false if time not synced
// Output buffer must be at least 6 bytes (5 chars + null terminator)
bool formatDate(char* output, size_t outputSize);

// Force NTP resync (useful after long WiFi disconnection)
void resyncTime();

// Get current time as unix timestamp (seconds since 1970-01-01)
// Returns 0 if not synced
time_t getCurrentTime();

#endif
