// ============================================================================
// led_indicator.cpp - Status LED control using hardware interrupts
// ============================================================================
// This library manages the onboard LED to indicate connectivity status:
// - 1 flash = WiFi + MQTT connected (healthy)
// - 2 flashes = WiFi connected, MQTT disconnected
// - 3 flashes = WiFi disconnected
// - Quick flashing = OTA update in progress
//
// Uses hardware timers (ESP32 Timer0 / ESP8266 Timer1) for freeze-proof
// operation even during blocking network operations.
// ============================================================================

// ============================================================================
// INCLUDES
// ============================================================================

#include "led_indicator.h"

#if defined(ESP8266)
  extern "C" {
    #include "user_interface.h"
  }
#endif

// ============================================================================
// STATE VARIABLES
// ============================================================================

// Hardware timer for LED updates
#if defined(ESP32)
  hw_timer_t *ledTimer = NULL;
  portMUX_TYPE ledTimerMux = portMUX_INITIALIZER_UNLOCKED;
#endif

// Connectivity state tracking
ConnectivityState currentConnectivity = {false, false};

// LED override state (for OTA updates)
bool ledOverrideActive = false;
LedPattern ledOverridePattern = LED_OFF;

// Current pattern and state (volatile for interrupt safety)
volatile LedPattern currentPattern = LED_OFF;
volatile uint8_t flashCount = 0;
volatile bool ledState = false;

// Debug counter to verify ISR is firing
volatile uint32_t ledIsrCount = 0;

// ============================================================================
// HARDWARE TIMER CALLBACK
// ============================================================================

// LED update callback - called by hardware interrupt timer every 100ms
// IRAM_ATTR ensures function is in RAM for ESP32/ESP8266 interrupt execution
void IRAM_ATTR ledTimerCallback() {
  #if defined(ESP32)
    portENTER_CRITICAL_ISR(&ledTimerMux);
  #endif
  
  // Debug: increment counter to verify ISR is firing
  ledIsrCount++;
  
  // DEBUG: Temporarily use 5s on / 5s off for all patterns to diagnose LED issue
  // 100ms timer interval, so 50 ticks = 5 seconds
  flashCount++;
  if (flashCount >= 100) {  // Full cycle = 10 seconds (50 on + 50 off)
    flashCount = 0;
  }
  
  if (flashCount < 50) {
    // First 5 seconds: LED ON
    if (!ledState) {
      ledState = true;
      ledOn();
    }
  } else {
    // Next 5 seconds: LED OFF
    if (ledState) {
      ledState = false;
      ledOff();
    }
  }
  
  // Original pattern logic commented out for debugging:
  /*
  switch (currentPattern) {
    case LED_OTA_PROGRESS:
      // Quick flashing (100ms on/off) during OTA
      ledState = !ledState;
      if (ledState) {
        ledOn();
      } else {
        ledOff();
      }
      break;
      
    case LED_CONNECTED:
      // 1 quick flash followed by 2 second pause
      if (flashCount == 0) {
        ledOn();
        flashCount = 1;
      } else if (flashCount == 1) {
        ledOff();
        flashCount = 2;
      } else {
        flashCount++;
        if (flashCount >= 20) {  // 20 * 100ms = 2 seconds
          flashCount = 0;
        }
      }
      break;

    case LED_MQTT_DISCONNECTED:
      // 2 quick flashes followed by 2 second pause
      if (flashCount < 4) {
        ledState = (flashCount % 2 == 0);
        if (ledState) {
          ledOn();
        } else {
          ledOff();
        }
        flashCount++;
      } else {
        flashCount++;
        if (flashCount >= 20) {
          flashCount = 0;
        }
      }
      break;
    
    case LED_WIFI_DISCONNECTED:
      // 3 quick flashes followed by 2 second pause
      if (flashCount < 6) {
        ledState = (flashCount % 2 == 0);
        if (ledState) {
          ledOn();
        } else {
          ledOff();
        }
        flashCount++;
      } else {
        flashCount++;
        if (flashCount >= 20) {
          flashCount = 0;
        }
      }
      break;
      
    case LED_OFF:
    default:
      ledOff();
      break;
  }
  */
  
  #if defined(ESP32)
    portEXIT_CRITICAL_ISR(&ledTimerMux);
  #endif
}

// ESP8266 Timer1 ISR wrapper
#if defined(ESP8266)
void ICACHE_RAM_ATTR onTimer1ISR() {
  ledTimerCallback();
  timer1_write(500000);  // Reset timer for next interrupt (100ms at 5MHz)
}
#endif

// ============================================================================
// INITIALIZATION
// ============================================================================

void initLedIndicator() {
  // Configure LED pin
  pinMode(LED_PIN, OUTPUT);
  ledOff();
  DEBUG_PRINT("LED indicator initialized on GPIO");
  DEBUG_PRINTLN(LED_PIN);
  
  // Initialize hardware timer
  #if defined(ESP32)
    // ESP32 Arduino Core 3.x changed the timer API significantly
    // Core 3.x: timerBegin(frequency), timerAlarm(timer, us, reload, count)
    // Core 2.x: timerBegin(num, divider, countUp), timerAlarmWrite(), timerAlarmEnable()
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      // ESP32 Arduino Core 3.x API
      ledTimer = timerBegin(1000000);  // 1MHz timer frequency
      timerAttachInterrupt(ledTimer, &ledTimerCallback);
      timerAlarm(ledTimer, 100000, true, 0);  // 100000us = 100ms, auto-reload, unlimited
      DEBUG_PRINTLN("ESP32 hardware timer initialized (Core 3.x, 100ms interval)");
    #else
      // ESP32 Arduino Core 2.x API
      ledTimer = timerBegin(0, 80, true);  // Timer 0, divider 80 (1MHz)
      timerAttachInterrupt(ledTimer, &ledTimerCallback, true);
      timerAlarmWrite(ledTimer, 100000, true);  // 100ms interval
      timerAlarmEnable(ledTimer);
      DEBUG_PRINTLN("ESP32 hardware timer initialized (Core 2.x, 100ms interval)");
    #endif
    
  #elif defined(ESP8266)
    // ESP8266: Use Timer1 (hardware timer)
    timer1_isr_init();
    timer1_attachInterrupt(onTimer1ISR);
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);  // 5MHz tick rate
    timer1_write(500000);  // 100ms at 5MHz
    DEBUG_PRINTLN("ESP8266 Timer1 initialized (100ms interval)");
  #endif

  // Start with "disconnected" status
  DEBUG_PRINTLN("[LED] Setting initial pattern to WIFI_DISCONNECTED");
  setLedPattern(LED_WIFI_DISCONNECTED);
  DEBUG_PRINTLN("[LED] initLedIndicator complete");
}

// ============================================================================
// PATTERN MANAGEMENT
// ============================================================================

// Helper to convert pattern to string for debugging
const char* patternToString(LedPattern pattern) {
  switch(pattern) {
    case LED_OTA_PROGRESS: return "OTA_PROGRESS";
    case LED_CONNECTED: return "CONNECTED";
    case LED_MQTT_DISCONNECTED: return "MQTT_DISCONNECTED";
    case LED_WIFI_DISCONNECTED: return "WIFI_DISCONNECTED";
    case LED_OFF: return "OFF";
    default: return "UNKNOWN";
  }
}

// Internal function to set LED pattern
void setLedPattern(LedPattern pattern) {
  DEBUG_PRINT("[LED] setLedPattern called: ");
  DEBUG_PRINT(patternToString(pattern));
  DEBUG_PRINT(" (current: ");
  DEBUG_PRINT(patternToString(currentPattern));
  DEBUG_PRINTLN(")");
  
  if (pattern == currentPattern) {
    DEBUG_PRINTLN("[LED] Pattern unchanged, skipping");
    return;
  }
  
  #if defined(ESP32)
    // Stop current timer
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      timerStop(ledTimer);
    #else
      timerAlarmDisable(ledTimer);
    #endif
    
    // Reset state (use critical section for volatile variables)
    portENTER_CRITICAL(&ledTimerMux);
    flashCount = 0;
    ledState = false;
    currentPattern = pattern;
    portEXIT_CRITICAL(&ledTimerMux);
    
    ledOff();
    
    // Start new pattern
    if (pattern != LED_OFF) {
      #if ESP_ARDUINO_VERSION_MAJOR >= 3
        timerStart(ledTimer);
        DEBUG_PRINTLN("[LED] Timer started (Core 3.x)");
      #else
        timerAlarmEnable(ledTimer);
        DEBUG_PRINTLN("[LED] Timer enabled (Core 2.x)");
      #endif
    } else {
      DEBUG_PRINTLN("[LED] Timer NOT started (pattern is OFF)");
    }
    
  #elif defined(ESP8266)
    // ESP8266: Timer1 is always running, just update the pattern
    noInterrupts();
    flashCount = 0;
    ledState = false;
    currentPattern = pattern;
    interrupts();
    
    ledOff();
  #endif
}

// Update LED pattern based on current connectivity state
void updateLedStatus() {
  DEBUG_PRINT("[LED] updateLedStatus: wifi=");
  DEBUG_PRINT(currentConnectivity.wifiConnected ? "true" : "false");
  DEBUG_PRINT(", mqtt=");
  DEBUG_PRINT(currentConnectivity.mqttConnected ? "true" : "false");
  DEBUG_PRINT(", override=");
  DEBUG_PRINTLN(ledOverrideActive ? "true" : "false");
  
  // If override is active (e.g., during OTA), don't change pattern
  if (ledOverrideActive) {
    DEBUG_PRINTLN("[LED] Override active, skipping update");
    return;
  }
  
  // Determine pattern based on connectivity state (priority order)
  LedPattern newPattern;
  if (!currentConnectivity.wifiConnected) {
    newPattern = LED_WIFI_DISCONNECTED;
  } else if (!currentConnectivity.mqttConnected) {
    newPattern = LED_MQTT_DISCONNECTED;
  } else {
    newPattern = LED_CONNECTED;
  }
  
  DEBUG_PRINT("[LED] Determined pattern: ");
  DEBUG_PRINTLN(patternToString(newPattern));
  
  setLedPattern(newPattern);
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Update connectivity state and refresh LED pattern
void setConnectivityState(bool wifiConnected, bool mqttConnected) {
  DEBUG_PRINT("[LED] setConnectivityState: wifi=");
  DEBUG_PRINT(wifiConnected ? "true" : "false");
  DEBUG_PRINT(", mqtt=");
  DEBUG_PRINTLN(mqttConnected ? "true" : "false");
  
  currentConnectivity.wifiConnected = wifiConnected;
  currentConnectivity.mqttConnected = mqttConnected;
  updateLedStatus();
}

// Begin LED override mode (for OTA updates)
void beginLedOverride(LedPattern pattern) {
  DEBUG_PRINT("[LED] beginLedOverride: ");
  DEBUG_PRINTLN(patternToString(pattern));
  
  ledOverrideActive = true;
  ledOverridePattern = pattern;
  setLedPattern(pattern);
}

// End LED override mode and restore normal connectivity indication
void endLedOverride() {
  DEBUG_PRINTLN("[LED] endLedOverride");
  
  ledOverrideActive = false;
  updateLedStatus();
}

// Debug function to check if timer ISR is firing
uint32_t getLedIsrCount() {
  return ledIsrCount;
}

// Debug function to get current LED state info
void debugLedState() {
  DEBUG_PRINT("[LED DEBUG] ISR count: ");
  DEBUG_PRINT(ledIsrCount);
  DEBUG_PRINT(", pattern: ");
  DEBUG_PRINT(patternToString(currentPattern));
  DEBUG_PRINT(", flashCount: ");
  DEBUG_PRINT(flashCount);
  DEBUG_PRINT(", ledState: ");
  DEBUG_PRINTLN(ledState ? "ON" : "OFF");
}
