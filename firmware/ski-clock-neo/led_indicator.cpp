#include "led_indicator.h"

#if defined(ESP8266)
  extern "C" {
    #include "user_interface.h"
  }
#endif

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

// LED update callback - called by hardware interrupt timer
// IRAM_ATTR ensures function is in RAM for ESP32/ESP8266 interrupt execution
void IRAM_ATTR ledTimerCallback() {
  #if defined(ESP32)
    portENTER_CRITICAL_ISR(&ledTimerMux);
  #endif
  
  switch (currentPattern) {
    case LED_OTA_PROGRESS:
      // Quick flashing (100ms on/off)
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
        // Pause - do nothing
        flashCount++;
        if (flashCount >= 20) {  // 20 * 100ms = 2 seconds
          flashCount = 0;
        }
      }
      break;

    case LED_MQTT_DISCONNECTED:
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
    
    case LED_WIFI_DISCONNECTED:
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
void initLedIndicator() {
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

  setLedPattern(LED_WIFI_DISCONNECTED);  // Start with "disconnected" status
}

// Internal function to physically set LED pattern (called by state management functions)
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

// Update LED pattern based on current connectivity state
// Uses priority ranking: WiFi disconnected > MQTT disconnected > Connected
void updateLedStatus() {
  // If override is active (e.g., during OTA), don't change pattern
  if (ledOverrideActive) {
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
  
  setLedPattern(newPattern);
}

// PUBLIC API: Update connectivity state and refresh LED pattern
// Call this from WiFi and MQTT event handlers
void setConnectivityState(bool wifiConnected, bool mqttConnected) {
  currentConnectivity.wifiConnected = wifiConnected;
  currentConnectivity.mqttConnected = mqttConnected;
  updateLedStatus();
}

// PUBLIC API: Begin LED override mode (for OTA updates)
// Saves current context and sets override pattern
void beginLedOverride(LedPattern pattern) {
  ledOverrideActive = true;
  ledOverridePattern = pattern;
  setLedPattern(pattern);
}

// PUBLIC API: End LED override mode
// Restores LED pattern based on actual connectivity state
void endLedOverride() {
  ledOverrideActive = false;
  updateLedStatus();  // Restore pattern based on actual connectivity
}
