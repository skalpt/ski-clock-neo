#ifndef NORRTEK_LED_INDICATOR_H
#define NORRTEK_LED_INDICATOR_H

#include <Arduino.h>
#include "debug.h"

enum LedPattern {
  LED_OTA_PROGRESS,
  LED_CONNECTED,
  LED_MQTT_DISCONNECTED,
  LED_WIFI_DISCONNECTED,
  LED_OFF
};

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

#if defined(CONFIG_IDF_TARGET_ESP32C3)
  #define LED_PIN 8
#elif !defined(LED_PIN)
  #define LED_PIN LED_BUILTIN
#endif

#if defined(ESP32) || defined(ESP8266)
  #define LED_GPIO_ON LOW
  #define LED_GPIO_OFF HIGH
#else
  #define LED_GPIO_ON HIGH
  #define LED_GPIO_OFF LOW
#endif

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

struct ConnectivityState {
  bool wifiConnected;
  bool mqttConnected;
};

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

void initLedIndicator();
void setLedPattern(LedPattern pattern);
void updateLedStatus();
void setConnectivityState(bool wifiConnected, bool mqttConnected);
void beginLedOverride(LedPattern pattern);
void endLedOverride();

void IRAM_ATTR ledTimerCallback();
#if defined(ESP8266)
  void ICACHE_RAM_ATTR onTimer1ISR();
#endif

#endif
