#ifndef DATA_BUTTON_H
#define DATA_BUTTON_H

#include <Arduino.h>

// Button press callback function type
typedef void (*ButtonCallback)();

// Initialize button using BUTTON_PIN from config with pull-up resistor
// Button should connect pin to GND when pressed (active LOW)
// Uses debounce of 50ms
void initButton();

// Set callback for button press events
// Callback will be called from ISR context - keep it short!
void setButtonPressCallback(ButtonCallback callback);

// Clear any pending button press flag (call during lockout to discard bounces)
void clearButtonPressed();


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
