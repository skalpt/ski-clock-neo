#include "data_temperature.h"
#include "display_controller.h"  // For updateTemperatureDisplay callback
#include "debug.h"
#include <OneWire.h>
#include <DallasTemperature.h>

#if defined(ESP32)
  #include <Ticker.h>  // ESP32: Software ticker (loop-driven)
#elif defined(ESP8266)
  #include <TickTwo.h>  // ESP8266: Software ticker (loop-driven, non-ISR, WiFi-safe)
#endif

// Global sensor objects (initialized in initTemperatureData)
static OneWire* oneWire = nullptr;
static DallasTemperature* sensors = nullptr;
static bool initialized = false;
static float lastTemperature = 0.0;
static bool lastReadValid = false;

// Temperature timing - platform specific
#if defined(ESP32)
  // ESP32: Use standard Ticker (loop-driven software ticker)
  static Ticker temperaturePollTicker;
  static Ticker temperatureReadTicker;
#elif defined(ESP8266)
  // ESP8266: Use TickTwo (loop-driven, non-ISR, WiFi-safe)
  void temperaturePollCallback();  // Forward declaration
  void temperatureReadCallback();  // Forward declaration
  TickTwo temperaturePollTicker(temperaturePollCallback, 30000, 0, MILLIS);  // 30s, endless (extern in .h)
  TickTwo temperatureReadTicker(temperatureReadCallback, 750, 1, MILLIS);  // 750ms, once (extern in .h)
#endif

static bool temperatureRequestPending = false;
static bool firstTemperatureRead = true;

void initTemperatureData(uint8_t pin) {
  DEBUG_PRINT("Initializing DS18B20 temperature sensor on GPIO ");
  DEBUG_PRINTLN(pin);
  
  // Initialize OneWire on specified pin
  oneWire = new OneWire(pin);
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
  
  // Start temperature polling ticker (30 seconds) - platform specific
  #if defined(ESP32)
    temperaturePollTicker.attach(30.0, temperaturePollCallback);
    DEBUG_PRINTLN("Temperature poll ticker started (ESP32 Ticker - 30s)");
  #elif defined(ESP8266)
    temperaturePollTicker.start();
    DEBUG_PRINTLN("Temperature poll ticker started (ESP8266 TickTwo - 30s, loop-driven)");
  #endif
  
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

// Temperature ticker callbacks (software tickers, loop-driven, safe for setText)
// ESP32: Software ticker (loop-driven via Ticker library)
// ESP8266: TickTwo (loop-driven, non-ISR, WiFi-safe)
void temperaturePollCallback() {
  if (!temperatureRequestPending) {
    // Request new temperature reading
    requestTemperature();
    temperatureRequestPending = true;
    DEBUG_PRINTLN("Temperature read requested (ticker)");
    
    // Schedule read after 750ms - platform specific
    #if defined(ESP32)
      temperatureReadTicker.once(0.75f, temperatureReadCallback);  // 0.75 seconds
    #elif defined(ESP8266)
      temperatureReadTicker.start();  // TickTwo: start 750ms one-shot timer
    #endif
  }
}

void temperatureReadCallback() {
  float temp;
  if (getTemperature(&temp)) {
    // Update display via callback to display_controller
    updateTemperatureDisplay();
    DEBUG_PRINT("Temperature updated: ");
    DEBUG_PRINTLN(temp);
    
    // First read complete
    if (firstTemperatureRead) {
      firstTemperatureRead = false;
      DEBUG_PRINTLN("First temperature read complete");
    }
  } else {
    // Failed to read temperature - retry on next poll
    DEBUG_PRINTLN("Temperature read failed, will retry on next poll");
  }
  
  // Always clear flag to allow next poll (ensures recovery from failed reads)
  temperatureRequestPending = false;
}
