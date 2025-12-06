// ============================================================================
// data_button.cpp - Button input handling with hardware debouncing
// ============================================================================
// This library manages button input with:
// - Hardware interrupt on FALLING edge (button press start)
// - Hardware interrupt on RISING edge (button release / noise filter)
// - 50ms debounce threshold to filter mains interference
// - Press callback only
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "data_button.h"             // This file's header
#include "../../ski-clock-neo_config.h" // For BUTTON_PIN configuration
#include "../core/event_log.h"       // For logging button events
#include "../core/debug.h"           // For debug logging

// ============================================================================
// CONSTANTS
// ============================================================================

static const unsigned long DEBOUNCE_MS = 50;  // Debounce threshold in milliseconds

// ============================================================================
// STATE VARIABLES
// ============================================================================

static uint8_t buttonPin = BUTTON_PIN;      // GPIO pin for button
static bool initialized = false;            // True after init

// Callbacks
static ButtonCallback pressCallback = nullptr;

// Debounce state (managed by updateButton polling)
static volatile bool edgeDetected = false;         // ISR sets this on any edge
static bool pressInProgress = false;               // True while button held down
static unsigned long pressStartTime = 0;           // When LOW state first detected
static bool pressHandled = false;                  // True after callback fired (prevent re-trigger)
static bool lastPinWasHigh = true;                 // Track last state to require release before re-arm

// ============================================================================
// INTERRUPT SERVICE ROUTINE
// ============================================================================

// IRAM_ATTR: Place ISR in IRAM for faster execution (ESP32/ESP8266)
// Minimal ISR - just sets flag, all logic happens in updateButton()
// This avoids calling digitalRead/millis which aren't IRAM-safe on ESP8266
void IRAM_ATTR buttonChangeISR() {
  edgeDetected = true;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initButton() {
  DEBUG_PRINT("Initializing button on GPIO ");
  DEBUG_PRINTLN(buttonPin);
  
  // Configure pin as input with pull-up (active LOW button)
  pinMode(buttonPin, INPUT_PULLUP);
  
  // Attach interrupt on CHANGE to detect both edges
  // ISR checks pin state to determine if falling (press) or rising (release)
  attachInterrupt(digitalPinToInterrupt(buttonPin), buttonChangeISR, CHANGE);
  
  initialized = true;
  DEBUG_PRINTLN("Button initialized with debouncing (50ms threshold)");
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Set callback for button press events
void setButtonPressCallback(ButtonCallback callback) {
  pressCallback = callback;
}

// Clear any pending button press state (call during lockout to discard bounces)
// This suppresses the current press and requires a full release before next press
void clearButtonPressed() {
  edgeDetected = false;
  pressInProgress = false;
  pressHandled = true;
  lastPinWasHigh = false;  // Require a HIGH transition before re-arming
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

// Process button press events with debouncing
// This handles all the logic that can't safely run in the ISR
void updateButton() {
  if (!initialized) {
    return;
  }
  
  // Read current pin state (safe to do in main loop)
  bool buttonIsLow = (digitalRead(buttonPin) == LOW);
  
  // Handle state transitions based on current pin state
  if (buttonIsLow) {
    // Button is currently pressed (LOW)
    // Only start a new press cycle if we saw a HIGH state first (prevents re-trigger)
    if (!pressInProgress && lastPinWasHigh) {
      // Just transitioned from HIGH to LOW - start debounce timer
      pressInProgress = true;
      pressStartTime = millis();
      pressHandled = false;
    }
    // Check if debounce time has passed and we haven't triggered yet
    else if (pressInProgress && !pressHandled && (millis() - pressStartTime >= DEBOUNCE_MS)) {
      pressHandled = true;  // Prevent re-trigger while button still held
      
      DEBUG_PRINTLN("Button pressed (debounced)");
      logEvent("button_press", nullptr);
      
      if (pressCallback != nullptr) {
        pressCallback();
      }
    }
    lastPinWasHigh = false;
  } else {
    // Button is released (HIGH) - reset state
    // This clears pressInProgress so noise spikes that release quickly
    // never reach the 50ms threshold
    pressInProgress = false;
    lastPinWasHigh = true;  // Allow next press to be detected
  }
  
  // Clear edge flag (we've processed it)
  edgeDetected = false;
}
