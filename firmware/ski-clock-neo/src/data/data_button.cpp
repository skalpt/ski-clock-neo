// ============================================================================
// data_button.cpp - Button input handling with fast hardware debouncing
// ============================================================================
// This library manages button input with:
// - Hardware interrupt on CHANGE edge with direct register read
// - Platform-optimized pin reading (GPIO.in for ESP32, GPI for ESP8266)
// - 50ms debounce threshold to filter mains interference
// - Edge detection and timestamping in ISR for instant response
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "data_button.h"             // This file's header
#include "../../ski-clock-neo_config.h" // For BUTTON_PIN configuration
#include "../core/event_log.h"       // For logging button events
#include "../core/debug.h"           // For debug logging

#if defined(ESP32)
  #include "driver/gpio.h"
  #include "soc/gpio_struct.h"
#elif defined(ESP8266)
  extern "C" {
    #include "eagle_soc.h"
  }
#endif

// ============================================================================
// CONSTANTS
// ============================================================================

static const unsigned long DEBOUNCE_MS = 50;  // Debounce threshold in milliseconds

// ============================================================================
// FAST PIN READ MACROS
// ============================================================================
// Direct register access for minimal ISR latency
// These are single bitwise operations - essentially instant

#if defined(ESP32)
  // ESP32: Read from GPIO.in register (handles GPIO 0-31)
  // For GPIO 32-39, would need GPIO.in1.val instead
  #define FAST_PIN_READ(pin) ((GPIO.in >> (pin)) & 1)
#elif defined(ESP8266)
  // ESP8266: Read from GPI register (GPIO input register)
  #define FAST_PIN_READ(pin) ((GPI >> (pin)) & 1)
#else
  // Fallback for other platforms
  #define FAST_PIN_READ(pin) digitalRead(pin)
#endif

// ============================================================================
// STATE VARIABLES
// ============================================================================

static uint8_t buttonPin = BUTTON_PIN;      // GPIO pin for button
static bool initialized = false;            // True after init

// Callbacks
static ButtonCallback pressCallback = nullptr;

// Debounce state (shared between ISR and updateButton)
// All variables accessed by ISR must be volatile
static volatile bool pressInProgress = false;        // True while button held down (set by ISR)
static volatile unsigned long pressStartTime = 0;   // When falling edge detected (set by ISR)
static volatile bool pressHandled = false;          // True after callback fired (set by both)

// ============================================================================
// INTERRUPT SERVICE ROUTINE
// ============================================================================

// IRAM_ATTR: Place ISR in IRAM for faster execution (ESP32/ESP8266)
// Uses direct register read for minimal latency - no function call overhead
void IRAM_ATTR buttonChangeISR() {
  // Direct register read - single bitwise operation, essentially instant
  bool pinIsLow = !FAST_PIN_READ(buttonPin);
  
  if (pinIsLow) {
    // FALLING edge detected - button press started
    if (!pressInProgress) {
      pressInProgress = true;
      pressStartTime = millis();  // Safe on both ESP32 and ESP8266 with IRAM_ATTR
      pressHandled = false;
    }
  } else {
    // RISING edge detected - button released (or noise spike ended)
    // This clears the press so noise that releases quickly never triggers
    pressInProgress = false;
  }
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
  // ISR uses direct register read to determine edge type
  attachInterrupt(digitalPinToInterrupt(buttonPin), buttonChangeISR, CHANGE);
  
  initialized = true;
  DEBUG_PRINTLN("Button initialized with fast debouncing (50ms threshold)");
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
  noInterrupts();  // Atomic update of volatile variables
  pressInProgress = false;
  pressHandled = true;
  interrupts();
}

// Check if button is currently pressed
bool isButtonPressed() {
  return !FAST_PIN_READ(buttonPin);  // Active LOW, inverted
}

// Get how long button has been held (returns 0 if not pressed)
uint32_t getButtonHoldTime() {
  if (pressInProgress && !pressHandled) {
    return millis() - pressStartTime;
  }
  return 0;
}

// ============================================================================
// UPDATE (call from timer or main loop)
// ============================================================================

// Process button press events with debouncing
// ISR handles edge detection and timing, this just checks threshold and fires callback
void updateButton() {
  if (!initialized) {
    return;
  }
  
  // Check if a press is in progress and debounce time has passed
  // The ISR already set pressInProgress=true on falling edge and will clear it on rising
  // We only need to check if enough time has passed while still held
  if (pressInProgress && !pressHandled) {
    unsigned long elapsed = millis() - pressStartTime;
    
    if (elapsed >= DEBOUNCE_MS) {
      // Debounce threshold reached while button still held - valid press!
      pressHandled = true;  // Prevent re-trigger while button still held
      
      DEBUG_PRINTLN("Button pressed (debounced)");
      logEvent("button_press", nullptr);
      
      if (pressCallback != nullptr) {
        pressCallback();
      }
    }
  }
}
