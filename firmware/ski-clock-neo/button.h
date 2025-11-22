#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

// Button press callback function type
typedef void (*ButtonCallback)();

// Initialize button on specified GPIO pin with pull-up resistor
// Button should connect pin to GND when pressed (active LOW)
// debounceMs: minimum time between button events (default 50ms)
void initButton(uint8_t pin, uint16_t debounceMs = 50);

// Set callback for button press events
// Callback will be called from ISR context - keep it short!
void setButtonPressCallback(ButtonCallback callback);

// Set callback for button release events
// Callback will be called from ISR context - keep it short!
void setButtonReleaseCallback(ButtonCallback callback);

// Check if button is currently pressed (debounced)
// Safe to call from any context
bool isButtonPressed();

// Get time button has been held down in milliseconds
// Returns 0 if button is not pressed
uint32_t getButtonHoldTime();

// Update button state (must be called regularly from loop)
// Handles debouncing and callback invocation
void updateButton();

#endif
