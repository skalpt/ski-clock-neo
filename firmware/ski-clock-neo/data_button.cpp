// ============================================================================
// data_button.cpp - Button input handling with debouncing
// ============================================================================
// This library manages button input with:
// - Hardware interrupt for immediate response
// - Software debouncing (50ms default)
// - Press and release callbacks
// - Hold time tracking
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "data_button.h"
#include "ski-clock-neo_config.h"
#include "event_log.h"
#include "debug.h"

// ============================================================================
// STATE VARIABLES
// ============================================================================

static uint8_t buttonPin = BUTTON_PIN;      // GPIO pin for button
static uint16_t debounceTime = 50;          // Debounce time in milliseconds
static bool buttonState = false;            // Current debounced state
static bool lastRawState = false;           // Last raw GPIO reading
static uint32_t lastChangeTime = 0;         // Last time state changed
static uint32_t pressTime = 0;              // When button was pressed
static bool initialized = false;            // True after init

// Callbacks
static ButtonCallback pressCallback = nullptr;
static ButtonCallback releaseCallback = nullptr;

// ISR flag (set by interrupt, cleared by updateButton)
static volatile bool interruptPending = false;

// ============================================================================
// INTERRUPT SERVICE ROUTINE
// ============================================================================

// IRAM_ATTR: Place ISR in IRAM for faster execution (ESP32/ESP8266)
void IRAM_ATTR buttonISR() {
  interruptPending = true;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initButton() {
  DEBUG_PRINT("Initializing button on GPIO ");
  DEBUG_PRINT(buttonPin);
  DEBUG_PRINT(" (debounce: ");
  DEBUG_PRINT(debounceTime);
  DEBUG_PRINTLN("ms)");
  
  // Configure pin as input with pull-up (active LOW button)
  pinMode(buttonPin, INPUT_PULLUP);
  
  // Attach interrupt on both edges (press and release)
  #if defined(ESP32)
    attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, CHANGE);
  #elif defined(ESP8266)
    attachInterrupt(digitalPinToInterrupt(buttonPin), buttonISR, CHANGE);
  #endif
  
  // Read initial state
  lastRawState = digitalRead(buttonPin);
  buttonState = lastRawState;
  lastChangeTime = millis();
  
  initialized = true;
  DEBUG_PRINTLN("Button initialized");
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Set callback for button press events
void setButtonPressCallback(ButtonCallback callback) {
  pressCallback = callback;
}

// Set callback for button release events
void setButtonReleaseCallback(ButtonCallback callback) {
  releaseCallback = callback;
}

// Check if button is currently pressed
bool isButtonPressed() {
  return buttonState == LOW;  // Active LOW
}

// Get how long button has been held (milliseconds)
uint32_t getButtonHoldTime() {
  if (!isButtonPressed()) {
    return 0;
  }
  return millis() - pressTime;
}

// ============================================================================
// UPDATE (call from main loop)
// ============================================================================

// Process button state changes with debouncing
void updateButton() {
  if (!initialized) {
    return;
  }
  
  // Only process if interrupt flag is set (optimization)
  if (!interruptPending) {
    return;
  }
  
  interruptPending = false;
  
  // Read current pin state
  bool rawState = digitalRead(buttonPin);
  
  // Check if state has changed
  if (rawState != lastRawState) {
    lastRawState = rawState;
    lastChangeTime = millis();
    return;  // Wait for debounce period
  }
  
  // Check if enough time has passed for debouncing
  uint32_t now = millis();
  if ((now - lastChangeTime) < debounceTime) {
    return;  // Still debouncing
  }
  
  // State is stable, check if it's different from buttonState
  if (rawState != buttonState) {
    buttonState = rawState;
    
    if (buttonState == LOW) {
      // Button pressed (active LOW)
      pressTime = now;
      DEBUG_PRINTLN("Button pressed");
      logEvent("button_press", nullptr);
      if (pressCallback != nullptr) {
        pressCallback();
      }
    } else {
      // Button released
      uint32_t holdTime = now - pressTime;
      DEBUG_PRINT("Button released (held for ");
      DEBUG_PRINT(holdTime);
      DEBUG_PRINTLN("ms)");
      
      // Log release with hold duration
      static char eventData[32];
      snprintf(eventData, sizeof(eventData), "{\"hold_ms\":%lu}", holdTime);
      logEvent("button_release", eventData);
      
      if (releaseCallback != nullptr) {
        releaseCallback();
      }
    }
  }
}
