#ifndef DATA_TEMPERATURE_H
#define DATA_TEMPERATURE_H

#include <Arduino.h>

// Initialize DS18B20 temperature sensor on specified GPIO pin
// Must be called once during setup
void initTemperatureData(uint8_t pin);

// Request a new temperature reading from the sensor
// This is non-blocking and starts the conversion process
// Call this, then wait ~750ms, then call getTemperature()
void requestTemperature();

// Get the last temperature reading in Celsius
// Returns true if valid, false if sensor error or not initialized
// Temperature range: -55°C to +125°C
bool getTemperature(float* temperature);

// Format temperature for display as "XX*C" (e.g., "12*C", "-5*C")
// The * will be rendered as degree symbol by the font renderer
// Returns true if successful, false if sensor error
// Output buffer must be at least 5 bytes (4 chars + null terminator)
bool formatTemperature(char* output, size_t outputSize);

// Check if temperature sensor is connected and responding
bool isSensorConnected();

// Update temperature tickers (ESP8266 only - loop-driven)
// Must be called in loop() on ESP8266 for TickTwo to work
// ESP32: No-op (uses standard Ticker)
void updateTemperatureData();

#endif
