// ============================================================================
// led_indicator.h - LED status indicator declarations
// ============================================================================
// Hardware interrupt-driven LED patterns for WiFi/MQTT status indication.
// Uses ESP32 hardware timers or ESP8266 Timer1 for freeze-proof operation.
// ============================================================================

#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <Arduino.h>

// ============================================================================
// LED PATTERNS
// ============================================================================

enum LedPattern {
  LED_OTA_PROGRESS,      // Fast blink (OTA in progress)
  LED_CONNECTED,         // 1 flash + pause (WiFi & MQTT connected)
  LED_MQTT_DISCONNECTED, // 2 flashes + pause (WiFi connected, MQTT disconnected)
  LED_WIFI_DISCONNECTED, // 3 flashes + pause (WiFi disconnected)
  LED_OFF                // No flashing
};

// ============================================================================
// CONNECTIVITY STATE
// ============================================================================

struct ConnectivityState {
  bool wifiConnected;
  bool mqttConnected;
};

// ============================================================================
// LED PIN CONFIGURATION
// ============================================================================

// Fallback for boards that don't define LED_BUILTIN
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2  // GPIO2 is standard for most ESP8266/ESP32 boards
#endif

// Board-specific LED pin overrides
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  #define LED_PIN 8  // ESP32-C3 uses GPIO8
#elif !defined(LED_PIN)
  #define LED_PIN LED_BUILTIN  // Default to LED_BUILTIN for other boards
#endif

// LED GPIO state - ESP32 and ESP8266 both use inverted logic (LOW = LED on)
#if defined(ESP32) || defined(ESP8266)
  #define LED_GPIO_ON LOW
  #define LED_GPIO_OFF HIGH
#else
  #define LED_GPIO_ON HIGH
  #define LED_GPIO_OFF LOW
#endif

// ============================================================================
// FAST GPIO MACROS (for interrupt context)
// ============================================================================

#if defined(ESP8266)
  #include <esp8266_peri.h>
  #define FAST_PIN_HIGH(pin) GPOS = (1 << (pin))
  #define FAST_PIN_LOW(pin)  GPOC = (1 << (pin))
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  #include <soc/gpio_reg.h>
  #define FAST_PIN_HIGH(pin) GPIO.out_w1ts.val = (1UL << (pin))
  #define FAST_PIN_LOW(pin)  GPIO.out_w1tc.val = (1UL << (pin))
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  #include <soc/gpio_reg.h>
  #define FAST_PIN_HIGH(pin) do { \
    if ((pin) < 32) { \
      GPIO.out_w1ts = (1UL << (pin)); \
    } else { \
      GPIO.out1_w1ts.val = (1UL << ((pin) - 32)); \
    } \
  } while(0)
  #define FAST_PIN_LOW(pin) do { \
    if ((pin) < 32) { \
      GPIO.out_w1tc = (1UL << (pin)); \
    } else { \
      GPIO.out1_w1tc.val = (1UL << ((pin) - 32)); \
    } \
  } while(0)
#elif defined(ESP32)
  #include <soc/gpio_reg.h>
  #define FAST_PIN_HIGH(pin) GPIO.out_w1ts = (1 << (pin))
  #define FAST_PIN_LOW(pin)  GPIO.out_w1tc = (1 << (pin))
#else
  #define FAST_PIN_HIGH(pin) digitalWrite(pin, HIGH)
  #define FAST_PIN_LOW(pin)  digitalWrite(pin, LOW)
#endif

// ============================================================================
// INLINE HELPER FUNCTIONS (safe to include in header)
// ============================================================================

inline void IRAM_ATTR ledOn() {
  if (LED_GPIO_ON == LOW) {
    FAST_PIN_LOW(LED_PIN);
  } else {
    FAST_PIN_HIGH(LED_PIN);
  }
}

inline void IRAM_ATTR ledOff() {
  if (LED_GPIO_ON == LOW) {
    FAST_PIN_HIGH(LED_PIN);
  } else {
    FAST_PIN_LOW(LED_PIN);
  }
}

// ============================================================================
// PUBLIC API - Function declarations
// ============================================================================

// Initialize LED indicator hardware and timer
void initLedIndicator();

// Set LED pattern directly (internal use - prefer setConnectivityState)
void setLedPattern(LedPattern pattern);

// Update LED based on current connectivity state
void updateLedStatus();

// PUBLIC API: Update connectivity state and refresh LED pattern
void setConnectivityState(bool wifiConnected, bool mqttConnected);

// PUBLIC API: Begin LED override mode (for OTA updates)
void beginLedOverride(LedPattern pattern);

// PUBLIC API: End LED override mode
void endLedOverride();

#endif // LED_INDICATOR_H
