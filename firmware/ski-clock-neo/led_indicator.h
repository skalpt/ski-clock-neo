#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include <Ticker.h>
#include "debug.h"

// LED patterns for WiFi status indication
enum LedPattern {
  LED_QUICK_FLASH,      // Fast blink during setup
  LED_ONE_FLASH,        // 1 flash + pause (WiFi & MQTT connected)
  LED_TWO_FLASH,        // 2 flashes + pause (WiFi connected, MQTT disconnected)
  LED_THREE_FLASH,      // 3 flashes + pause (WiFi disconnected)
  LED_OFF               // No flashing
};

// Ticker for checking the current status for the LED indicator
Ticker ledStatusTicker;

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

// Helper functions to handle LED on/off with inverted logic
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

// Hardware timer for LED updates
#if defined(ESP32)
  hw_timer_t *ledTimer = NULL;
  portMUX_TYPE ledTimerMux = portMUX_INITIALIZER_UNLOCKED;
#elif defined(ESP8266)
  // ESP8266 uses Timer1 (hardware timer)
  extern "C" {
    #include "user_interface.h"
  }
#endif

// Current pattern and state (volatile for interrupt safety)
volatile LedPattern currentPattern = LED_OFF;
volatile uint8_t flashCount = 0;
volatile bool ledState = false;

// LED update callback - called by hardware interrupt timer
// IRAM_ATTR ensures function is in RAM for ESP32/ESP8266 interrupt execution
void IRAM_ATTR ledTimerCallback() {
  #if defined(ESP32)
    portENTER_CRITICAL_ISR(&ledTimerMux);
  #endif
  
  switch (currentPattern) {
    case LED_QUICK_FLASH:
      // Quick flashing (100ms on/off) for setup
      ledState = !ledState;
      if (ledState) {
        ledOn();
      } else {
        ledOff();
      }
      break;
      
    case LED_ONE_FLASH:
      // 1 quick flash followed by 2 second pause
      if (flashCount == 0) {
        ledOn();
        flashCount = 1;
      } else if (flashCount == 1) {
        ledOff();
        flashCount = 2;
      } else {
        // Pause - do nothing
        flashCount++;
        if (flashCount >= 20) {  // 20 * 100ms = 2 seconds
          flashCount = 0;
        }
      }
      break;

    case LED_TWO_FLASH:
      // 2 quick flashes followed by 2 second pause
      if (flashCount < 4) {
        // Flash 2 times (on/off/on/off)
        ledState = (flashCount % 2 == 0);
        if (ledState) {
          ledOn();
        } else {
          ledOff();
        }
        flashCount++;
      } else {
        // Pause
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
          ledOn();
        } else {
          ledOff();
        }
        flashCount++;
      } else {
        // Pause
        flashCount++;
        if (flashCount >= 20) {  // 20 * 100ms = 2 seconds
          flashCount = 0;
        }
      }
      break;
      
    case LED_OFF:
    default:
      ledOff();
      break;
  }
  
  #if defined(ESP32)
    portEXIT_CRITICAL_ISR(&ledTimerMux);
  #endif
  }
  
  // ESP8266 Timer1 ISR wrapper
  #if defined(ESP8266)
  void ICACHE_RAM_ATTR onTimer1ISR() {
    ledTimerCallback();
    timer1_write(500000);  // Reset timer for next interrupt (100ms at 5MHz = 500,000 ticks)
  }
#endif

// Initialize LED indicator
void setupLedIndicator() {
  pinMode(LED_PIN, OUTPUT);
  ledOff();
  DEBUG_PRINT("LED indicator initialized on GPIO");
  DEBUG_PRINTLN(LED_PIN);
  
  // Initialize hardware timer
  #if defined(ESP32)
    // ESP32: Use hardware timer 0, divider 80 (1MHz tick rate)
    ledTimer = timerBegin(0, 80, true);  // Timer 0, divider 80, count up
    timerAttachInterrupt(ledTimer, &ledTimerCallback, true);  // Attach ISR, edge triggered
    timerAlarmWrite(ledTimer, 100000, true);  // 100ms interval (100,000 microseconds), auto-reload
    // Don't enable yet - will enable when pattern is set
    DEBUG_PRINTLN("ESP32 hardware timer initialized (100ms interval)");
    
  #elif defined(ESP8266)
    // ESP8266: Use Timer1 (hardware timer)
    timer1_isr_init();
    timer1_attachInterrupt(onTimer1ISR);
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);  // 80MHz / 16 = 5MHz, edge, loop mode
    timer1_write(500000);  // 500,000 ticks = 100ms at 5MHz
    // Timer1 starts automatically
    DEBUG_PRINTLN("ESP8266 Timer1 initialized (100ms interval)");
  #endif

  setLedPattern(LED_QUICK_FLASH);  // Set LED indicator to setup status
}

// Set LED pattern
void setLedPattern(LedPattern pattern) {
  if (pattern == currentPattern) {
    return;  // No change needed
  }
  
  #if defined(ESP32)
    // Stop current timer
    timerAlarmDisable(ledTimer);
    
    // Reset state (use critical section for volatile variables)
    portENTER_CRITICAL(&ledTimerMux);
    flashCount = 0;
    ledState = false;
    currentPattern = pattern;
    portEXIT_CRITICAL(&ledTimerMux);
    
    ledOff();
    
    // Start new pattern
    if (pattern != LED_OFF) {
      // Enable hardware timer for all active patterns
      timerAlarmEnable(ledTimer);
    }
    
  #elif defined(ESP8266)
    // ESP8266: Timer1 is always running, just update the pattern
    // Use noInterrupts/interrupts for critical section
    noInterrupts();
    flashCount = 0;
    ledState = false;
    currentPattern = pattern;
    interrupts();
    
    ledOff();
    
    // Timer1 keeps running regardless of pattern (LED_OFF just turns it off in ISR)
  #endif
}

// Enable periodic status updates (called when WiFi/MQTT is ready)
void enableLedStatusTicker() {
  ledStatusTicker.attach_ms(1000, updateLedStatus);
  DEBUG_PRINTLN("LED status ticker enabled (1 second interval)");
  updateLedStatus();
}

// Update LED pattern based on WiFi & MQTT status (called from ticker)
void updateLedStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    if (mqttClient.connected()) {
      setLedPattern(LED_ONE_FLASH);
    } else {
      setLedPattern(LED_TWO_FLASH);
    }
  } else {
    setLedPattern(LED_THREE_FLASH);
  }
}

#endif
