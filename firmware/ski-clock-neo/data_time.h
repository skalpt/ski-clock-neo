#ifndef DATA_TIME_H
#define DATA_TIME_H

#include <Arduino.h>

// DS3231 RTC Support
// Requires: RTClib library (Adafruit) - install via Arduino Library Manager
// The RTC provides instant time on boot before WiFi/NTP connects.
// NTP will periodically update the RTC to keep it accurate.

// Initialize time system (RTC + NTP)
// - Detects DS3231 on I2C bus
// - If RTC has valid time, uses it immediately
// - Starts NTP sync in background
void initTimeData();

// Check if time is available (from RTC or NTP)
bool isTimeSynced();

// Check if RTC is present and operational
bool isRtcAvailable();

// Format current time as "hh.mm" (e.g., "09.53", "23.59")
// Returns true if successful, false if time not synced
// Output buffer must be at least 6 bytes (5 chars + null terminator)
bool formatTime(char* output, size_t outputSize);

// Format current date as "dd-mm" (e.g., "01-01", "31-12")
// Returns true if successful, false if time not synced
// Output buffer must be at least 6 bytes (5 chars + null terminator)
bool formatDate(char* output, size_t outputSize);

// Force NTP resync (useful after long WiFi disconnection)
// Also updates RTC if NTP sync succeeds
void resyncTime();

// Get current time as unix timestamp (seconds since 1970-01-01)
// Returns 0 if not synced
time_t getCurrentTime();

// Sync RTC from NTP (called automatically when NTP syncs)
// Can also be called manually if needed
void syncRtcFromNtp();

#endif
