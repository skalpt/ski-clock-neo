#include "data_time.h"

#include "debug.h"
#include <time.h>
#include <sys/time.h>
#include <Wire.h>
#include <RTClib.h>

// Sweden timezone: CET-1CEST,M3.5.0,M10.5.0/3
// CET (UTC+1) from last Sunday of October to last Sunday of March
// CEST (UTC+2) from last Sunday of March to last Sunday of October
const char* SWEDEN_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";

// NTP servers (prioritize European servers for Sweden)
const char* NTP_SERVER_1 = "se.pool.ntp.org";
const char* NTP_SERVER_2 = "europe.pool.ntp.org";
const char* NTP_SERVER_3 = "pool.ntp.org";

// RTC object
static RTC_DS3231 rtc;

// State tracking
static bool timeInitialized = false;
static bool rtcAvailable = false;
static bool rtcTimeValid = false;
static bool ntpSynced = false;
static unsigned long lastNtpCheck = 0;
static unsigned long lastRtcSync = 0;

// Check intervals (milliseconds)
const unsigned long NTP_CHECK_INTERVAL = 10000;    // Check NTP every 10 seconds until synced
const unsigned long RTC_SYNC_INTERVAL = 3600000;   // Sync RTC from NTP every hour

// Minimum valid timestamp (2020-01-01 00:00:00 UTC)
const time_t MIN_VALID_TIME = 1577836800;

void initTimeData() {
  DEBUG_PRINTLN("Initializing time system (RTC + NTP)");
  
  // Initialize I2C for RTC
  Wire.begin();
  
  // Try to initialize DS3231 RTC
  if (rtc.begin()) {
    rtcAvailable = true;
    DEBUG_PRINTLN("DS3231 RTC found on I2C bus");
    
    // Check if RTC lost power (time invalid)
    if (rtc.lostPower()) {
      DEBUG_PRINTLN("RTC lost power - time invalid, waiting for NTP sync");
      rtcTimeValid = false;
    } else {
      // Check if RTC time is reasonable (after 2020)
      DateTime rtcTime = rtc.now();
      if (rtcTime.unixtime() > MIN_VALID_TIME) {
        rtcTimeValid = true;
        DEBUG_PRINT("RTC time valid: ");
        DEBUG_PRINT(rtcTime.year());
        DEBUG_PRINT("-");
        DEBUG_PRINT(rtcTime.month());
        DEBUG_PRINT("-");
        DEBUG_PRINT(rtcTime.day());
        DEBUG_PRINT(" ");
        DEBUG_PRINT(rtcTime.hour());
        DEBUG_PRINT(":");
        DEBUG_PRINT(rtcTime.minute());
        DEBUG_PRINT(":");
        DEBUG_PRINTLN(rtcTime.second());
        
        // Set system time from RTC (UTC)
        struct timeval tv;
        tv.tv_sec = rtcTime.unixtime();
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        DEBUG_PRINTLN("System time set from RTC");
      } else {
        DEBUG_PRINTLN("RTC time invalid (before 2020), waiting for NTP sync");
        rtcTimeValid = false;
      }
    }
  } else {
    rtcAvailable = false;
    DEBUG_PRINTLN("DS3231 RTC not found - using NTP only");
  }
  
  // Initialize NTP (will sync in background)
  DEBUG_PRINTLN("Initializing NTP time sync for Sweden (Europe/Stockholm)");
  
  #if defined(ESP32)
    configTzTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #elif defined(ESP8266)
    configTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #endif
  
  timeInitialized = true;
  DEBUG_PRINTLN("Time system initialized");
}

bool isTimeSynced() {
  if (!timeInitialized) return false;
  
  // Check if NTP has synced
  if (!ntpSynced) {
    // Check more frequently if RTC is invalid (needs immediate sync)
    // Otherwise use interval to reduce overhead
    bool shouldCheck = !rtcTimeValid || (millis() - lastNtpCheck > NTP_CHECK_INTERVAL);
    
    if (shouldCheck) {
      lastNtpCheck = millis();
      time_t now = time(nullptr);
      if (now > MIN_VALID_TIME) {
        ntpSynced = true;
        DEBUG_PRINTLN("NTP sync detected");
        
        // Update RTC from NTP immediately if available
        if (rtcAvailable) {
          syncRtcFromNtp();
        }
      }
    }
  }
  
  // If RTC has valid time, we're synced
  if (rtcAvailable && rtcTimeValid) {
    return true;
  }
  
  // No valid RTC - check NTP sync
  time_t now = time(nullptr);
  if (now > MIN_VALID_TIME) {
    return true;
  }
  
  return false;
}

bool isRtcAvailable() {
  return rtcAvailable;
}

void syncRtcFromNtp() {
  if (!rtcAvailable) return;
  
  time_t now = time(nullptr);
  if (now <= MIN_VALID_TIME) {
    DEBUG_PRINTLN("Cannot sync RTC - NTP time not valid");
    return;
  }
  
  // Get UTC time (RTC stores UTC)
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  
  // Update RTC
  rtc.adjust(DateTime(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  ));
  
  rtcTimeValid = true;
  lastRtcSync = millis();
  
  DEBUG_PRINT("RTC synced from NTP: ");
  DEBUG_PRINT(timeinfo.tm_year + 1900);
  DEBUG_PRINT("-");
  DEBUG_PRINT(timeinfo.tm_mon + 1);
  DEBUG_PRINT("-");
  DEBUG_PRINT(timeinfo.tm_mday);
  DEBUG_PRINT(" ");
  DEBUG_PRINT(timeinfo.tm_hour);
  DEBUG_PRINT(":");
  DEBUG_PRINT(timeinfo.tm_min);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(timeinfo.tm_sec);
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
  
  // Periodic RTC sync from NTP (every hour)
  if (rtcAvailable && ntpSynced && (millis() - lastRtcSync > RTC_SYNC_INTERVAL)) {
    syncRtcFromNtp();
  }
  
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
  snprintf(output, outputSize, "%02d-%02d", timeinfo.tm_mday, timeinfo.tm_mon + 1);
  return true;
}

void resyncTime() {
  DEBUG_PRINTLN("Forcing NTP resync");
  ntpSynced = false;
  lastNtpCheck = 0;  // Reset so next isTimeSynced() check triggers immediately
  lastRtcSync = 0;   // Reset so RTC updates immediately when NTP syncs
  
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
