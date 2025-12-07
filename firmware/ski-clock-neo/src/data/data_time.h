#ifndef DATA_TIME_H
#define DATA_TIME_H

#include <Arduino.h>

// DS3231 RTC Support
// Requires: RTClib library (Adafruit) - install via Arduino Library Manager
// The RTC provides instant time on boot before WiFi/NTP connects.
// NTP will periodically update the RTC to keep it accurate.

// Time change flags (can be OR'd together)
#define TIME_CHANGE_MINUTE  0x01
#define TIME_CHANGE_DATE    0x02

// Callback type for time change notifications
// Called when minute or date changes
// flags: TIME_CHANGE_MINUTE and/or TIME_CHANGE_DATE
typedef void (*TimeChangeCallback)(uint8_t flags);

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

// Get Unix timestamp for an event that occurred at a given millis() value
// Uses current time minus elapsed milliseconds to calculate when the event happened
// Returns the calculated timestamp, or 0 if time is not synced
// This is useful for adding accurate timestamps to queued MQTT messages
time_t getTimestampForEvent(uint32_t event_millis);

// Sync RTC from NTP (called automatically when NTP syncs)
// Can also be called manually if needed
void syncRtcFromNtp();

// Register callback for time changes (minute or date change)
// Callback is called with flags indicating what changed
void setTimeChangeCallback(TimeChangeCallback callback);

// Check for time changes and call callback if registered
// Should be called periodically (e.g., every second or in main loop)
// Returns true if a change was detected
bool checkTimeChange();

#endif
