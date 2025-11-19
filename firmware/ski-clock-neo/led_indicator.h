#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <Ticker.h>

// LED patterns for WiFi status indication
enum LedPattern {
  LED_QUICK_FLASH,      // Fast blink during setup
  LED_ONE_FLASH,        // 1 flash + pause (connected in loop)
  LED_THREE_FLASH,      // 3 flashes + pause (disconnected in loop)
  LED_OFF               // No flashing
};

// LED pin - use built-in LED
// ESP32: typically GPIO 2 or 13, ESP8266: GPIO 2 (built-in)
#if defined(ESP32)
  #define LED_PIN 2  // Most ESP32 boards use GPIO2 for built-in LED
#elif defined(ESP8266)
  #define LED_PIN 2  // ESP8266 built-in LED (inverted logic - LOW = on)
#else
  #define LED_PIN LED_BUILTIN
#endif

// LED state (inverted for ESP8266 where LOW = LED on)
#if defined(ESP8266)
  #define LED_ON LOW
  #define LED_OFF HIGH
#else
  #define LED_ON HIGH
  #define LED_OFF LOW
#endif

// Ticker for LED updates
Ticker ledTicker;

// Current pattern and state (volatile for ISR safety)
volatile LedPattern currentPattern = LED_OFF;
volatile bool ledUpdateFlag = false;
volatile uint8_t flashCount = 0;
volatile bool ledState = false;

// Ticker callback - ONLY set flag (ISR-safe)
void ledTimerCallback() {
  ledUpdateFlag = true;
}

// Initialize LED system
void setupLED() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);
  Serial.println("LED indicator initialized");
}

// Update LED based on current pattern (called from loop when flag set)
void handleLedUpdate() {
  switch (currentPattern) {
    case LED_QUICK_FLASH:
      // Quick flashing (100ms on/off) for setup
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF);
      break;
      
    case LED_ONE_FLASH:
      // 1 quick flash followed by 2 second pause
      if (flashCount == 0) {
        digitalWrite(LED_PIN, LED_ON);
        flashCount = 1;
      } else if (flashCount == 1) {
        digitalWrite(LED_PIN, LED_OFF);
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
        digitalWrite(LED_PIN, ledState ? LED_ON : LED_OFF);
        flashCount++;
      } else {
        // Pause
        digitalWrite(LED_PIN, LED_OFF);
        flashCount++;
        if (flashCount >= 26) {  // 6 + 20 = 26 (2 second pause)
          flashCount = 0;
        }
      }
      break;
      
    case LED_OFF:
    default:
      digitalWrite(LED_PIN, LED_OFF);
      break;
  }
}

// Set LED pattern (non-ISR safe - call from loop only)
void setLedPattern(LedPattern pattern) {
  if (pattern == currentPattern) {
    return;  // No change needed
  }
  
  // Stop current timer
  ledTicker.detach();
  
  // Reset state (atomic writes to volatile variables)
  flashCount = 0;
  ledState = false;
  digitalWrite(LED_PIN, LED_OFF);
  
  // Start new pattern (atomic write)
  currentPattern = pattern;
  
  switch (pattern) {
    case LED_QUICK_FLASH:
      // 100ms interval for quick flashing
      ledTicker.attach_ms(100, ledTimerCallback);
      Serial.println("LED: Quick flash (setup mode)");
      break;
      
    case LED_ONE_FLASH:
      // 100ms interval for flash timing
      ledTicker.attach_ms(100, ledTimerCallback);
      Serial.println("LED: 1 flash pattern (WiFi connected)");
      break;
      
    case LED_THREE_FLASH:
      // 100ms interval for flash timing
      ledTicker.attach_ms(100, ledTimerCallback);
      Serial.println("LED: 3 flash pattern (WiFi disconnected)");
      break;
      
    case LED_OFF:
    default:
      digitalWrite(LED_PIN, LED_OFF);
      Serial.println("LED: Off");
      break;
  }
}

// Update LED based on WiFi status (call from loop)
void updateLedStatus() {
  // Check and handle LED updates from ticker
  if (ledUpdateFlag) {
    ledUpdateFlag = false;
    handleLedUpdate();
  }
  
  // Update pattern based on WiFi status (throttled to prevent spam)
  static unsigned long lastPatternCheck = 0;
  if (millis() - lastPatternCheck > 1000) {  // Check every second
    lastPatternCheck = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
      setLedPattern(LED_ONE_FLASH);
    } else {
      setLedPattern(LED_THREE_FLASH);
    }
  }
}

#endif
