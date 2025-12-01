// ============================================================================
// data_time.cpp - RTC and NTP time management
// ============================================================================
// This library manages time using DS3231 RTC and NTP:
// - RTC provides instant time on boot (before WiFi/NTP connects)
// - NTP syncs RTC hourly to maintain accuracy
// - Falls back gracefully to NTP-only if no RTC present
// - Detects minute and date changes via 1-second polling timer
// - Configured for Sweden timezone (CET/CEST)
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "data_time.h"               // This file's header
#include "../ski-clock-neo_config.h" // For RTC pin definitions
#include "../core/event_log.h"       // For logging time events
#include "../core/debug.h"           // For debug logging
#include "../core/timer_helpers.h"   // For 1-second polling timer
#include <time.h>                    // For time functions
#include <sys/time.h>                // For settimeofday
#include <Wire.h>                    // For I2C communication with RTC
#include <RTClib.h>                  // For DS3231 RTC library

// ============================================================================
// CONSTANTS
// ============================================================================

// Sweden timezone: CET (UTC+1) winter, CEST (UTC+2) summer
const char* SWEDEN_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";

// NTP servers (prioritize European servers for Sweden)
const char* NTP_SERVER_1 = "se.pool.ntp.org";
const char* NTP_SERVER_2 = "europe.pool.ntp.org";
const char* NTP_SERVER_3 = "pool.ntp.org";

// Check intervals (milliseconds)
const unsigned long NTP_CHECK_INTERVAL = 10000;    // Check NTP every 10 seconds until synced
const unsigned long RTC_SYNC_INTERVAL = 3600000;   // Sync RTC from NTP every hour

// Minimum valid timestamp (2020-01-01 00:00:00 UTC)
const time_t MIN_VALID_TIME = 1577836800;

// ============================================================================
// STATE VARIABLES
// ============================================================================

static RTC_DS3231 rtc;                              // RTC object
static bool timeInitialized = false;                // True after init
static bool rtcAvailable = false;                   // True if RTC found on I2C
static bool rtcTimeValid = false;                   // True if RTC has valid time
static bool ntpSynced = false;                      // True after first NTP sync
static unsigned long lastNtpCheck = 0;              // Last NTP check timestamp
static unsigned long lastRtcSync = 0;               // Last RTC sync timestamp

// Time change detection
static int8_t lastMinute = -1;                      // Last known minute (-1 = uninitialized)
static int8_t lastDay = -1;                         // Last known day of month
static TimeChangeCallback timeChangeCallback = nullptr;

// ============================================================================
// INITIALIZATION
// ============================================================================

void initTimeData() {
  DEBUG_PRINTLN("Initializing time system (RTC + NTP)");
  
  // Initialize I2C for RTC on custom pins (avoid ESP32-C3 default GPIO 8/9)
  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
  
  // Try to initialize DS3231 RTC
  if (rtc.begin()) {
    rtcAvailable = true;
    DEBUG_PRINTLN("DS3231 RTC found on I2C bus");
    logEvent("rtc_initialized", nullptr);
    
    // Check if RTC lost power (time invalid)
    if (rtc.lostPower()) {
      DEBUG_PRINTLN("RTC lost power - time invalid, waiting for NTP sync");
      rtcTimeValid = false;
      logEvent("rtc_lost_power", nullptr);
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
        logEvent("rtc_time_invalid", nullptr);
      }
    }
  } else {
    rtcAvailable = false;
    DEBUG_PRINTLN("DS3231 RTC not found - using NTP only");
    logEvent("rtc_not_found", nullptr);
  }
  
  // Initialize NTP (syncs in background)
  DEBUG_PRINTLN("Initializing NTP time sync for Sweden (Europe/Stockholm)");
  
  #if defined(ESP32)
    configTzTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #elif defined(ESP8266)
    configTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #endif
  
  // Create 1-second timer for time change detection
  createTimer("TimeCheck", 1000, []() { checkTimeChange(); });
  
  timeInitialized = true;
  DEBUG_PRINTLN("Time system initialized");
}

// ============================================================================
// RTC/NTP SYNC
// ============================================================================

// Sync RTC from NTP (called hourly after NTP is available)
void syncRtcFromNtp() {
  if (!rtcAvailable) return;
  
  time_t now = time(nullptr);
  if (now <= MIN_VALID_TIME) {
    DEBUG_PRINTLN("Cannot sync RTC - NTP time not valid");
    logEvent("rtc_sync_failed", "{\"reason\":\"ntp_invalid\"}");
    return;
  }
  
  // Get UTC time (RTC stores UTC)
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  
  // Read current RTC time to calculate drift
  DateTime rtcTime = rtc.now();
  int32_t driftSeconds = (int32_t)now - (int32_t)rtcTime.unixtime();
  
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
  
  // Log drift correction if significant (>1 second)
  if (abs(driftSeconds) > 1) {
    static char eventData[48];
    snprintf(eventData, sizeof(eventData), "{\"drift_seconds\":%ld}", driftSeconds);
    logEvent("rtc_drift_corrected", eventData);
    DEBUG_PRINT("RTC drift corrected: ");
    DEBUG_PRINT(driftSeconds);
    DEBUG_PRINTLN(" seconds");
  }
  
  logEvent("rtc_synced_from_ntp", nullptr);
}

// ============================================================================
// TIME CHANGE DETECTION
// ============================================================================

// Check for minute or date changes (called every 1 second)
bool checkTimeChange() {
  if (!isTimeSynced()) {
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
  
  uint8_t changeFlags = 0;
  
  // Check minute change
  if (lastMinute != timeinfo.tm_min) {
    if (lastMinute >= 0) {  // Skip first call (initialization)
      changeFlags |= TIME_CHANGE_MINUTE;
      DEBUG_PRINT("Minute changed: ");
      DEBUG_PRINT(lastMinute);
      DEBUG_PRINT(" -> ");
      DEBUG_PRINTLN(timeinfo.tm_min);
    }
    lastMinute = timeinfo.tm_min;
  }
  
  // Check date change (day of month)
  if (lastDay != timeinfo.tm_mday) {
    if (lastDay >= 0) {  // Skip first call (initialization)
      changeFlags |= TIME_CHANGE_DATE;
      DEBUG_PRINT("Date changed: day ");
      DEBUG_PRINT(lastDay);
      DEBUG_PRINT(" -> ");
      DEBUG_PRINTLN(timeinfo.tm_mday);
    }
    lastDay = timeinfo.tm_mday;
  }
  
  // Call callback if registered and changes detected
  if (changeFlags != 0 && timeChangeCallback != nullptr) {
    timeChangeCallback(changeFlags);
  }
  
  return changeFlags != 0;
}

// Set callback for time change events
void setTimeChangeCallback(TimeChangeCallback callback) {
  timeChangeCallback = callback;
  DEBUG_PRINTLN("Time change callback registered");
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Check if time is available (either from RTC or NTP)
bool isTimeSynced() {
  if (!timeInitialized) return false;
  
  // Check if NTP has synced
  if (!ntpSynced) {
    // Check more frequently if RTC is invalid (needs immediate sync)
    bool shouldCheck = !rtcTimeValid || (millis() - lastNtpCheck > NTP_CHECK_INTERVAL);
    
    if (shouldCheck) {
      lastNtpCheck = millis();
      time_t now = time(nullptr);
      if (now > MIN_VALID_TIME) {
        ntpSynced = true;
        DEBUG_PRINTLN("NTP sync detected");
        logEvent("ntp_sync_success", nullptr);
        
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

// Check if RTC is available
bool isRtcAvailable() {
  return rtcAvailable;
}

// Format time as "hh.mm"
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

// Format date as "dd-mm"
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

// Force NTP resync
void resyncTime() {
  DEBUG_PRINTLN("Forcing NTP resync");
  ntpSynced = false;
  lastNtpCheck = 0;
  lastRtcSync = 0;
  
  #if defined(ESP32)
    configTzTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #elif defined(ESP8266)
    configTime(SWEDEN_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  #endif
}

// Get current time as Unix timestamp
time_t getCurrentTime() {
  if (!isTimeSynced()) {
    return 0;
  }
  return time(nullptr);
}
