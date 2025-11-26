#include "data_temperature.h"

#include "ski-clock-neo_config.h" // For TEMPERATURE_PIN
#include "display_controller.h"   // For updateTemperatureDisplay callback
#include "event_log.h"            // For logging temperature events
#include "timer_task.h"           // For unified timer abstraction
#include "debug.h"                // For serial debugging
#include <OneWire.h>              // OneWire library for DS18B20
#include <DallasTemperature.h>    // Main library for DS18B20

// Forward declarations for timer callbacks
void temperaturePollCallback();
void temperatureReadCallback();

// Global sensor objects (initialized in initTemperatureData)
static OneWire* oneWire = nullptr;
static DallasTemperature* sensors = nullptr;
static bool initialized = false;
static float lastTemperature = 0.0;
static bool lastReadValid = false;

static bool temperatureRequestPending = false;
static bool firstTemperatureRead = true;

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
  }
  
  // Set resolution to 12-bit for best accuracy (conversion time ~750ms)
  sensors->setResolution(12);
  
  // Disable blocking wait - we'll handle conversion time manually
  sensors->setWaitForConversion(false);
  
  initialized = true;
  
  // Create temperature timers using timer_task library
  // Poll timer: 30-second interval for triggering temperature conversions
  createTimer("TempPoll", 30000, temperaturePollCallback);
  
  // Read timer: 750ms one-shot, triggered after conversion starts
  createOneShotTimer("TempRead", 750, temperatureReadCallback);
  
  // Trigger first poll immediately (requests conversion and schedules read after 750ms)
  // Clear flag first to ensure callback executes
  temperatureRequestPending = false;
  temperaturePollCallback();
  DEBUG_PRINTLN("Temperature sensor initialized (non-blocking mode, first poll triggered)");
}

void requestTemperature() {
  if (!initialized || sensors == nullptr) {
    return;
  }
  
  // Start asynchronous temperature conversion
  sensors->requestTemperatures();
}

bool getTemperature(float* temperature) {
  if (!initialized || sensors == nullptr || temperature == nullptr) {
    return false;
  }
  
  // Get temperature from first sensor (index 0)
  float tempC = sensors->getTempCByIndex(0);
  
  // Check if reading is valid (DS18B20 returns -127°C on error)
  if (tempC == DEVICE_DISCONNECTED_C || tempC < -55.0 || tempC > 125.0) {
    lastReadValid = false;
    return false;
  }
  
  *temperature = tempC;
  lastTemperature = tempC;
  lastReadValid = true;
  return true;
}

bool formatTemperature(char* output, size_t outputSize) {
  if (!initialized || !lastReadValid || output == nullptr || outputSize < 5) {
    return false;
  }
  
  // Round to nearest integer (correctly handles negative temperatures)
  // lroundf() rounds toward nearest integer for both positive and negative
  int tempInt = (int)lroundf(lastTemperature);
  
  // Format as "XX*C" where * will be rendered as degree symbol
  // Handle negative temperatures (e.g., "-5*C")
  snprintf(output, outputSize, "%d*C", tempInt);
  return true;
}

bool isSensorConnected() {
  if (!initialized || sensors == nullptr) {
    return false;
  }
  
  return (sensors->getDeviceCount() > 0);
}

// Temperature timer callbacks (managed by timer_task library)
void temperaturePollCallback() {
  if (!temperatureRequestPending) {
    // Request new temperature reading
    requestTemperature();
    temperatureRequestPending = true;
    DEBUG_PRINTLN("Temperature read requested (timer)");
    
    // Trigger one-shot read timer (750ms delay for sensor conversion)
    triggerTimer("TempRead");
  }
}

void temperatureReadCallback() {
  float temp;
  if (getTemperature(&temp)) {
    // Update display via callback to display_controller
    updateTemperatureDisplay();
    DEBUG_PRINT("Temperature updated: ");
    DEBUG_PRINTLN(temp);
    
    // Log temperature_read event with temperature value
    // Use dtostrf for portable float-to-string conversion
    char tempStr[16];
    dtostrf(temp, 4, 1, tempStr);
    String tempData = "{\"celsius\":";
    tempData += tempStr;
    tempData += "}";
    logEvent("temperature_read", tempData.c_str());
    
    // First read complete
    if (firstTemperatureRead) {
      firstTemperatureRead = false;
      DEBUG_PRINTLN("First temperature read complete");
    }
  } else {
    // Failed to read temperature - retry on next poll
    DEBUG_PRINTLN("Temperature read failed, will retry on next poll");
    logEvent("temperature_error", "{\"reason\":\"read_failed\"}");
  }
  
  // Always clear flag to allow next poll (ensures recovery from failed reads)
  temperatureRequestPending = false;
}
