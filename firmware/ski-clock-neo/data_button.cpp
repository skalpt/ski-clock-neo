#include "data_button.h"

#include "ski-clock-neo_config.h"         // For BUTTON_PIN
#include "debug.h"                        // For serial debugging

// Button state tracking
static uint8_t buttonPin = BUTTON_PIN;
static uint16_t debounceTime = 50;
static bool buttonState = false;          // Current debounced state
static bool lastRawState = false;         // Last raw GPIO reading
static uint32_t lastChangeTime = 0;       // Last time state changed
static uint32_t pressTime = 0;            // When button was pressed
static bool initialized = false;

// Callbacks
static ButtonCallback pressCallback = nullptr;
static ButtonCallback releaseCallback = nullptr;

// ISR flag (set by interrupt, cleared by updateButton)
static volatile bool interruptPending = false;

// IRAM_ATTR: Place ISR in IRAM for faster execution (ESP32/ESP8266)
void IRAM_ATTR buttonISR() {
  interruptPending = true;
}

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

void setButtonPressCallback(ButtonCallback callback) {
  pressCallback = callback;
}

void setButtonReleaseCallback(ButtonCallback callback) {
  releaseCallback = callback;
}

bool isButtonPressed() {
  return buttonState == LOW;  // Active LOW
}

uint32_t getButtonHoldTime() {
  if (!isButtonPressed()) {
    return 0;
  }
  return millis() - pressTime;
}

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
      if (pressCallback != nullptr) {
        pressCallback();
      }
    } else {
      // Button released
      uint32_t holdTime = now - pressTime;
      DEBUG_PRINT("Button released (held for ");
      DEBUG_PRINT(holdTime);
      DEBUG_PRINTLN("ms)");
      if (releaseCallback != nullptr) {
        releaseCallback();
      }
    }
  }
}
