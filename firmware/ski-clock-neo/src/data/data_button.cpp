// ============================================================================
// data_button.cpp - Button input handling with FALLING edge detection
// ============================================================================
// This library manages button input with:
// - Hardware interrupt on FALLING edge (button press)
// - Simple flag-based detection (no debouncing)
// - Press callback only
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "data_button.h"             // This file's header
#include "../../ski-clock-neo_config.h" // For BUTTON_PIN configuration
#include <norrtek_iot.h>             // Library: event_log, debug

// ============================================================================
// STATE VARIABLES
// ============================================================================

static uint8_t buttonPin = BUTTON_PIN;      // GPIO pin for button
static bool initialized = false;            // True after init

// Callbacks
static ButtonCallback pressCallback = nullptr;

// ISR flag (set by interrupt, cleared by updateButton)
static volatile bool buttonPressed = false;

// ============================================================================
// INTERRUPT SERVICE ROUTINE
// ============================================================================

// IRAM_ATTR: Place ISR in IRAM for faster execution (ESP32/ESP8266)
void IRAM_ATTR buttonISR() {
  buttonPressed = true;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initButton() {
  DEBUG_PRINT("Initializing button on GPIO ");
  DEBUG_PRINTLN(buttonPin);
  
  // Configure pin as input with pull-up (active LOW button)
  pinMode(buttonPin, INPUT_PULLUP);
  
  // Attach interrupt on FALLING edge only (button press)
  #if defined(ESP32)
    attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, FALLING);
  #elif defined(ESP8266)
    attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, FALLING);
  #endif
  
  initialized = true;
  DEBUG_PRINTLN("Button initialized (FALLING edge)");
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Set callback for button press events
void setButtonPressCallback(ButtonCallback callback) {
  pressCallback = callback;
}


// Check if button is currently pressed
bool isButtonPressed() {
  return digitalRead(buttonPin) == LOW;  // Active LOW
}

// Get how long button has been held (not implemented in simplified version)
uint32_t getButtonHoldTime() {
  return 0;
}

// ============================================================================
// UPDATE (call from timer or main loop)
// ============================================================================

// Process button press events
void updateButton() {
  if (!initialized) {
    return;
  }
  
  // Check if button was pressed (flag set by ISR)
  if (buttonPressed) {
    buttonPressed = false;
    
    DEBUG_PRINTLN("Button pressed");
    logEvent("button_press", nullptr);
    
    if (pressCallback != nullptr) {
      pressCallback();
    }
  }
}
