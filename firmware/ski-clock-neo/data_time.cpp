#include "data_time.h"

#include "debug.h"                // For serial debugging
#include <time.h>                 // For time functions

// Sweden timezone: CET-1CEST,M3.5.0,M10.5.0/3
// CET (UTC+1) from last Sunday of October to last Sunday of March
// CEST (UTC+2) from last Sunday of March to last Sunday of October
const char* SWEDEN_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";

// NTP servers (prioritize European servers for Sweden)
const char* NTP_SERVER_1 = "se.pool.ntp.org";   // Sweden NTP pool
const char* NTP_SERVER_2 = "europe.pool.ntp.org"; // European NTP pool
const char* NTP_SERVER_3 = "pool.ntp.org";      // Global NTP pool

// Track initialization state
static bool timeInitialized = false;

void initTimeData() {
  DEBUG_PRINTLN("Initializing NTP time sync for Sweden (Europe/Stockholm)");
  
  #if defined(ESP32)
    // ESP32: Use configTzTime with timezone string
    configTzTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #elif defined(ESP8266)
    // ESP8266: Use configTime with timezone offset in seconds
    // Note: ESP8266 uses POSIX timezone strings differently
    configTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #endif
  
  timeInitialized = true;
  DEBUG_PRINTLN("NTP time sync initialized");
}

bool isTimeSynced() {
  if (!timeInitialized) return false;
  
  time_t now = time(nullptr);
  
  // If time is before 2020-01-01, it's not synced yet
  // (ESP boots with time set to 1970-01-01)
  return (now > 1577836800); // 2020-01-01 00:00:00 UTC
}

bool formatTime(char* output, size_t outputSize) {
  if (!isTimeSynced() || outputSize < 6) {
    return false;
  }
  
  time_t now = time(nullptr);
  struct tm timeinfo;
  
  #if defined(ESP32)
    if (!getLocalTime(&timeinfo)) {
      return false;
    }
  #elif defined(ESP8266)
    localtime_r(&now, &timeinfo);
  #endif
  
  // Format as "hh.mm" (with leading zeros)
  snprintf(output, outputSize, "%02d.%02d", timeinfo.tm_hour, timeinfo.tm_min);
  return true;
}

bool formatDate(char* output, size_t outputSize) {
  if (!isTimeSynced() || outputSize < 6) {
    return false;
  }
  
  time_t now = time(nullptr);
  struct tm timeinfo;
  
  #if defined(ESP32)
    if (!getLocalTime(&timeinfo)) {
      return false;
    }
  #elif defined(ESP8266)
    localtime_r(&now, &timeinfo);
  #endif
  
  // Format as "dd-mm" (with leading zeros)
  // tm_mon is 0-based (0=January), tm_mday is 1-based
  snprintf(output, outputSize, "%02d-%02d", timeinfo.tm_mday, timeinfo.tm_mon + 1);
  return true;
}

void resyncTime() {
  DEBUG_PRINTLN("Forcing NTP resync");
  
  #if defined(ESP32)
    configTzTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #elif defined(ESP8266)
    configTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #endif
}

time_t getCurrentTime() {
  if (!isTimeSynced()) {
    return 0;
  }
  return time(nullptr);
}
