#ifndef DATA_BUTTON_H
#define DATA_BUTTON_H

#include <Arduino.h>

// Button press callback function type
typedef void (*ButtonCallback)();

// Initialize button using BUTTON_PIN from config with pull-up resistor
// Button should connect pin to GND when pressed (active LOW)
// Uses hardware debouncing: FALLING edge sets flag, RISING edge clears it
// Press only triggers if button held LOW for >= 50ms (filters mains noise)
void initButton();

// Set callback for button press events
// Callback is called from main loop context (not ISR), safe for longer operations
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
