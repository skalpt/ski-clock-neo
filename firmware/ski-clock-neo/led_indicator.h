#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <Ticker.h>

// LED patterns for WiFi status indication
enum LedPattern {
  LED_QUICK_FLASH,      // Fast blink during setup
  LED_ONE_FLASH,        // 1 flash + pause (connected)
  LED_THREE_FLASH,      // 3 flashes + pause (disconnected)
  LED_OFF               // No flashing
};

// Use LED_BUILTIN (can be overridden per-board if needed)
#ifndef LED_PIN
  #define LED_PIN LED_BUILTIN
#endif

// LED state - ESP32 and ESP8266 both use inverted logic (LOW = LED on)
#if defined(ESP8266) || defined(ESP32)
  #define LED_ON LOW
  #define LED_OFF HIGH
#else
  #define LED_ON HIGH
  #define LED_OFF LOW
#endif

// Direct port manipulation for fast GPIO operations in interrupt context
#if defined(ESP8266)
  #include <esp8266_peri.h>
  #define FAST_PIN_HIGH(pin) GPOS = (1 << (pin))
  #define FAST_PIN_LOW(pin)  GPOC = (1 << (pin))
#elif defined(ESP32)
  #include <soc/gpio_reg.h>
  #define FAST_PIN_HIGH(pin) GPIO.out_w1ts = (1 << (pin))
  #define FAST_PIN_LOW(pin)  GPIO.out_w1tc = (1 << (pin))
#else
  // Fallback to digitalWrite for other platforms
  #define FAST_PIN_HIGH(pin) digitalWrite(pin, HIGH)
  #define FAST_PIN_LOW(pin)  digitalWrite(pin, LOW)
#endif

// Ticker for LED updates (interrupt-driven for responsiveness)
Ticker ledTicker;

// Current pattern and state (volatile for interrupt safety)
volatile LedPattern currentPattern = LED_OFF;
volatile uint8_t flashCount = 0;
volatile bool ledState = false;

// Initialize LED system
void setupLED() {
  pinMode(LED_PIN, OUTPUT);
  if (LED_ON == LOW) {
    FAST_PIN_HIGH(LED_PIN);  // Start with LED off (inverted logic)
  } else {
    FAST_PIN_LOW(LED_PIN);
  }
  Serial.println("LED indicator initialized");
}

// LED update callback - called by interrupt ticker
// Uses fast port manipulation for safety in interrupt context
void ledTimerCallback() {
  switch (currentPattern) {
    case LED_QUICK_FLASH:
      // Quick flashing (100ms on/off) for setup
      ledState = !ledState;
      if (ledState) {
        if (LED_ON == LOW) FAST_PIN_LOW(LED_PIN);
        else FAST_PIN_HIGH(LED_PIN);
      } else {
        if (LED_ON == LOW) FAST_PIN_HIGH(LED_PIN);
        else FAST_PIN_LOW(LED_PIN);
      }
      break;
      
    case LED_ONE_FLASH:
      // 1 quick flash followed by 2 second pause
      if (flashCount == 0) {
        if (LED_ON == LOW) FAST_PIN_LOW(LED_PIN);
        else FAST_PIN_HIGH(LED_PIN);
        flashCount = 1;
      } else if (flashCount == 1) {
        if (LED_ON == LOW) FAST_PIN_HIGH(LED_PIN);
        else FAST_PIN_LOW(LED_PIN);
        flashCount = 2;
      } else {
        // Pause - do nothing
        flashCount++;
        if (flashCount >= 20) {  // 20 * 100ms = 2 seconds
          flashCount = 0;
        }
      }
      break;
      
    case LED_THREE_FLASH:
      // 3 quick flashes followed by 2 second pause
      if (flashCount < 6) {
        // Flash 3 times (on/off/on/off/on/off)
        ledState = (flashCount % 2 == 0);
        if (ledState) {
          if (LED_ON == LOW) FAST_PIN_LOW(LED_PIN);
          else FAST_PIN_HIGH(LED_PIN);
        } else {
          if (LED_ON == LOW) FAST_PIN_HIGH(LED_PIN);
          else FAST_PIN_LOW(LED_PIN);
        }
        flashCount++;
      } else {
        // Pause
        if (LED_ON == LOW) FAST_PIN_HIGH(LED_PIN);
        else FAST_PIN_LOW(LED_PIN);
        flashCount++;
        if (flashCount >= 26) {  // 6 + 20 = 26 (2 second pause)
          flashCount = 0;
        }
      }
      break;
      
    case LED_OFF:
    default:
      if (LED_ON == LOW) FAST_PIN_HIGH(LED_PIN);
      else FAST_PIN_LOW(LED_PIN);
      break;
  }
}

// Set LED pattern
void setLedPattern(LedPattern pattern) {
  if (pattern == currentPattern) {
    return;  // No change needed
  }
  
  // Stop current timer
  ledTicker.detach();
  
  // Reset state
  flashCount = 0;
  ledState = false;
  if (LED_ON == LOW) {
    FAST_PIN_HIGH(LED_PIN);  // LED off (inverted)
  } else {
    FAST_PIN_LOW(LED_PIN);
  }
  
  // Start new pattern
  currentPattern = pattern;
  
  switch (pattern) {
    case LED_QUICK_FLASH:
    case LED_ONE_FLASH:
    case LED_THREE_FLASH:
      // 100ms interval for all patterns (interrupt-driven ticker)
      ledTicker.attach_ms(100, ledTimerCallback);
      break;
      
    case LED_OFF:
    default:
      // Ticker already detached, LED already off
      break;
  }
}

// Update LED pattern based on WiFi status (call from loop)
void updateLedStatus() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 1000) {  // Check every second
    lastCheck = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
      setLedPattern(LED_ONE_FLASH);
    } else {
      setLedPattern(LED_THREE_FLASH);
    }
  }
}

#endif
