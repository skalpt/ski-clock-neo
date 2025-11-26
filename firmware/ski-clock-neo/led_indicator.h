#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <Arduino.h>
#include "debug.h"

// LED patterns for WiFi status indication
enum LedPattern {
  LED_OTA_PROGRESS,      // Fast blink (OTA in progress)
  LED_CONNECTED,         // 1 flash + pause (WiFi & MQTT connected)
  LED_MQTT_DISCONNECTED, // 2 flashes + pause (WiFi connected, MQTT disconnected)
  LED_WIFI_DISCONNECTED, // 3 flashes + pause (WiFi disconnected)
  LED_OFF                // No flashing
};

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

// Direct port manipulation for fast GPIO operations in interrupt context
#if defined(ESP8266)
  #include <esp8266_peri.h>
  #define FAST_PIN_HIGH(pin) GPOS = (1 << (pin))
  #define FAST_PIN_LOW(pin)  GPOC = (1 << (pin))
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  // ESP32-C3 has only 22 GPIOs (bank 0 only, no out1 registers)
  #include <soc/gpio_reg.h>
  #define FAST_PIN_HIGH(pin) GPIO.out_w1ts.val = (1UL << (pin))
  #define FAST_PIN_LOW(pin)  GPIO.out_w1tc.val = (1UL << (pin))
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32-S3 has 48 GPIOs (needs dual GPIO banks for pins 0-31 and 32-48)
  // Bank 0: direct uint32_t (no .val), Bank 1: struct with .val
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
  // ESP32 original - direct register access
  #include <soc/gpio_reg.h>
  #define FAST_PIN_HIGH(pin) GPIO.out_w1ts = (1 << (pin))
  #define FAST_PIN_LOW(pin)  GPIO.out_w1tc = (1 << (pin))
#else
  // Fallback to digitalWrite for other platforms
  #define FAST_PIN_HIGH(pin) digitalWrite(pin, HIGH)
  #define FAST_PIN_LOW(pin)  digitalWrite(pin, LOW)
#endif

// Helper macros to handle LED on/off with inverted logic
// Implemented as macros (not inline functions) to guarantee IRAM placement when called from ISR
#define ledOn() do { \
  if (LED_GPIO_ON == LOW) { FAST_PIN_LOW(LED_PIN); } \
  else { FAST_PIN_HIGH(LED_PIN); } \
} while(0)

#define ledOff() do { \
  if (LED_GPIO_ON == LOW) { FAST_PIN_HIGH(LED_PIN); } \
  else { FAST_PIN_LOW(LED_PIN); } \
} while(0)

// Connectivity state tracking
struct ConnectivityState {
  bool wifiConnected;
  bool mqttConnected;
};

// External variable declarations (definitions in led_indicator.cpp)
#if defined(ESP32)
  extern hw_timer_t *ledTimer;
  extern portMUX_TYPE ledTimerMux;
#endif

extern ConnectivityState currentConnectivity;
extern bool ledOverrideActive;
extern LedPattern ledOverridePattern;
extern volatile LedPattern currentPattern;
extern volatile uint8_t flashCount;
extern volatile bool ledState;

// Function declarations
void initLedIndicator();
void setLedPattern(LedPattern pattern);
void updateLedStatus();
void setConnectivityState(bool wifiConnected, bool mqttConnected);
void beginLedOverride(LedPattern pattern);
void endLedOverride();

// Debug functions
uint32_t getLedIsrCount();
void debugLedState();

// ISR callbacks (declared but not exposed as public API)
void IRAM_ATTR ledTimerCallback();
#if defined(ESP8266)
  void ICACHE_RAM_ATTR onTimer1ISR();
#endif

#endif
