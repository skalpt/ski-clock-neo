#include "data_temperature.h"
#include "display_controller.h"  // For updateTemperatureDisplay callback
#include "debug.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Ticker.h>  // For temperature timing

// Global sensor objects (initialized in initTemperatureData)
static OneWire* oneWire = nullptr;
static DallasTemperature* sensors = nullptr;
static bool initialized = false;
static float lastTemperature = 0.0;
static bool lastReadValid = false;

// Temperature timing tickers
static Ticker temperaturePollTicker;    // 30-second polling ticker
static Ticker temperatureReadTicker;    // 750ms read delay ticker
static bool temperatureRequestPending = false;
static bool firstTemperatureRead = true;

// Forward declarations
void temperaturePollCallback();
void temperatureReadCallback();

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
  
  // Kick off initial temperature conversion immediately
  // (don't wait 30 seconds for first reading)
  sensors->requestTemperatures();
  temperatureRequestPending = true;
  DEBUG_PRINTLN("Temperature sensor initialized (non-blocking mode, initial conversion started)");
  
  // Start temperature polling ticker (30 seconds)
  temperaturePollTicker.attach(30.0, temperaturePollCallback);
  DEBUG_PRINTLN("Temperature poll ticker started (30 seconds)");
  
  // Trigger first poll immediately (schedules read after 750ms)
  temperaturePollCallback();
  DEBUG_PRINTLN("Initial temperature poll triggered");
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

// Temperature ticker callbacks (software tickers, not ISR context)
void temperaturePollCallback() {
  if (!temperatureRequestPending) {
    // Request new temperature reading
    requestTemperature();
    temperatureRequestPending = true;
    DEBUG_PRINTLN("Temperature read requested (ticker)");
    
    // Schedule read after 750ms
    temperatureReadTicker.once(0.75, temperatureReadCallback);
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
  }
  temperatureRequestPending = false;
}
