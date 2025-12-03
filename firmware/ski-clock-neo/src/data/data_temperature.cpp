// ============================================================================
// data_temperature.cpp - DS18B20 temperature sensor management
// ============================================================================
// This library handles the DS18B20 temperature sensor using non-blocking reads:
// - 30-second polling interval for temperature requests
// - 750ms one-shot delay for sensor conversion (12-bit resolution)
// - Automatic display update via callback to display_controller
// - Event logging for temperature readings
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "data_temperature.h"              // This file's header
#include "../../ski-clock-neo_config.h"    // For TEMPERATURE_PIN definition
#include "../display/display_controller.h" // For display callback
#include "../core/event_log.h"             // For logging temperature events
#include "../core/timer_helpers.h"         // For timer management
#include "../core/debug.h"                 // For debug logging
#include "../core/device_config.h"         // For temperature offset
#include <OneWire.h>                       // OneWire library for DS18B20 communication
#include <DallasTemperature.h>             // DallasTemperature library for DS18B20

// ============================================================================
// STATE VARIABLES
// ============================================================================

static OneWire* oneWire = nullptr;              // OneWire bus instance
static DallasTemperature* sensors = nullptr;    // DallasTemperature library instance
static bool initialized = false;                // True after successful init
static float lastTemperature = 0.0;             // Last valid temperature reading
static bool lastReadValid = false;              // True if last reading was valid
static bool temperatureRequestPending = false;  // True while waiting for conversion
static bool firstTemperatureRead = true;        // True until first successful read

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void temperaturePollCallback();
void temperatureReadCallback();

// ============================================================================
// TIMER CALLBACKS
// ============================================================================

// Called every 30 seconds to start a new temperature conversion
void temperaturePollCallback() {
  if (!temperatureRequestPending) {
    // Request new temperature reading
    requestTemperature();
    temperatureRequestPending = true;
    DEBUG_PRINTLN("Temperature read requested (timer)");
    
    // Trigger one-shot read timer (750ms delay for sensor conversion)
    triggerTimer("TemperatureRead");
  }
}

// Called 750ms after temperature request to read the result
void temperatureReadCallback() {
  float temp;
  if (getTemperature(&temp)) {
    // Update display via callback to display_controller
    updateTemperatureDisplay();
    DEBUG_PRINT("Temperature updated: ");
    DEBUG_PRINTLN(temp);
    
    // Log temperature_read event with temperature value
    static char tempEventData[32];
    snprintf(tempEventData, sizeof(tempEventData), "{\"celsius\":%.1f}", temp);
    logEvent("temperature_read", tempEventData);
    
    // First read complete
    if (firstTemperatureRead) {
      firstTemperatureRead = false;
      DEBUG_PRINTLN("First temperature read complete");
    }
  } else {
    // Failed to read temperature - retry on next poll
    // (specific error already logged by getTemperature)
    DEBUG_PRINTLN("Temperature read failed, will retry on next poll");
  }
  
  // Always clear flag to allow next poll (ensures recovery from failed reads)
  temperatureRequestPending = false;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initTemperatureData() {
  DEBUG_PRINT("Initializing DS18B20 temperature sensor on GPIO ");
  DEBUG_PRINTLN(TEMPERATURE_PIN);
  
  // Initialize OneWire on specified pin
  oneWire = new OneWire(TEMPERATURE_PIN);
  sensors = new DallasTemperature(oneWire);
  
  // Start the sensor library
  sensors->begin();
  
  // Check how many devices are connected
  uint8_t deviceCount = sensors->getDeviceCount();
  DEBUG_PRINT("Found ");
  DEBUG_PRINT(deviceCount);
  DEBUG_PRINTLN(" DS18B20 device(s)");
  
  if (deviceCount == 0) {
    DEBUG_PRINTLN("WARNING: No DS18B20 sensor detected!");
    logEvent("temp_sensor_not_found", nullptr);
  }
  
  // Set resolution to 12-bit for best accuracy (conversion time ~750ms)
  sensors->setResolution(12);
  
  // Disable blocking wait - we handle conversion time with one-shot timer
  sensors->setWaitForConversion(false);
  
  initialized = true;
  
  // Create temperature timers using timing_helpers library
  // Poll timer: 30-second interval for triggering temperature conversions
  createTimer("TemperaturePoll", 30000, temperaturePollCallback);
  
  // Read timer: 750ms one-shot, triggered after conversion starts
  createOneShotTimer("TemperatureRead", 750, temperatureReadCallback);
  
  // Trigger first poll immediately
  temperatureRequestPending = false;
  temperaturePollCallback();
  DEBUG_PRINTLN("Temperature sensor initialized (non-blocking mode, first poll triggered)");
}

// ============================================================================
// SENSOR OPERATIONS
// ============================================================================

// Request temperature conversion (non-blocking)
void requestTemperature() {
  if (!initialized || sensors == nullptr) {
    return;
  }
  sensors->requestTemperatures();
}

// Read temperature from sensor (call 750ms after request)
bool getTemperature(float* temperature) {
  if (!initialized || sensors == nullptr || temperature == nullptr) {
    DEBUG_PRINTLN("getTemperature: Not initialized or null pointer");
    return false;
  }
  
  // Get temperature from first sensor (index 0)
  float tempC = sensors->getTempCByIndex(0);
  
  // Check for device disconnected error (DS18B20 returns -127°C)
  if (tempC == DEVICE_DISCONNECTED_C) {
    DEBUG_PRINTLN("Temperature sensor disconnected");
    lastReadValid = false;
    logEvent("temp_sensor_not_found", nullptr);
    return false;
  }
  
  // Check for out-of-range values (DS18B20 valid range: -55°C to +125°C)
  // Values outside this range indicate CRC errors or power issues
  if (tempC < -55.0 || tempC > 125.0) {
    DEBUG_PRINT("Temperature read invalid, raw value: ");
    DEBUG_PRINTLN(tempC);
    lastReadValid = false;
    
    // 85°C is the power-on reset value - indicates underpowered sensor
    if (tempC == 85.0) {
      static char eventData[48];
      snprintf(eventData, sizeof(eventData), "{\"raw\":%.1f,\"reason\":\"power_on_reset\"}", tempC);
      logEvent("temp_read_invalid", eventData);
    } else {
      static char eventData[48];
      snprintf(eventData, sizeof(eventData), "{\"raw\":%.1f,\"reason\":\"out_of_range\"}", tempC);
      logEvent("temp_read_invalid", eventData);
    }
    return false;
  }
  
  // Apply calibration offset from device config (stored in NVS/EEPROM)
  tempC += getTemperatureOffset();
  
  *temperature = tempC;
  lastTemperature = tempC;
  lastReadValid = true;
  return true;
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Format temperature as string (e.g., "23*C" or "-5*C")
bool formatTemperature(char* output, size_t outputSize) {
  if (!initialized || !lastReadValid || output == nullptr || outputSize < 5) {
    return false;
  }
  
  // Round to nearest integer (correctly handles negative temperatures)
  int tempInt = (int)lroundf(lastTemperature);
  
  // Format as "XX*C" where * will be rendered as degree symbol
  snprintf(output, outputSize, "%d*C", tempInt);
  return true;
}

// Check if sensor is connected
bool isSensorConnected() {
  if (!initialized || sensors == nullptr) {
    return false;
  }
  return (sensors->getDeviceCount() > 0);
}
