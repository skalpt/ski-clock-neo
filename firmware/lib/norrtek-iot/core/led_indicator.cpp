#include "led_indicator.h"

#if defined(ESP8266)
  extern "C" {
    #include "user_interface.h"
  }
#endif

#if defined(ESP32)
  hw_timer_t *ledTimer = NULL;
  portMUX_TYPE ledTimerMux = portMUX_INITIALIZER_UNLOCKED;
#endif

ConnectivityState currentConnectivity = {false, false};

bool ledOverrideActive = false;
LedPattern ledOverridePattern = LED_OFF;

volatile LedPattern currentPattern = LED_OFF;
volatile uint8_t flashCount = 0;
volatile bool ledState = false;

void IRAM_ATTR ledTimerCallback() {
  #if defined(ESP32)
    portENTER_CRITICAL_ISR(&ledTimerMux);
  #endif
  
  switch (currentPattern) {
    case LED_OTA_PROGRESS:
      ledState = !ledState;
      if (ledState) {
        ledOn();
      } else {
        ledOff();
      }
      break;
      
    case LED_CONNECTED:
      if (flashCount == 0) {
        ledOn();
        flashCount = 1;
      } else if (flashCount == 1) {
        ledOff();
        flashCount = 2;
      } else {
        flashCount++;
        if (flashCount >= 20) {
          flashCount = 0;
        }
      }
      break;

    case LED_MQTT_DISCONNECTED:
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
  
  #if defined(ESP32)
    portEXIT_CRITICAL_ISR(&ledTimerMux);
  #endif
}

#if defined(ESP8266)
void ICACHE_RAM_ATTR onTimer1ISR() {
  ledTimerCallback();
  timer1_write(500000);
}
#endif

void initLedIndicator() {
  pinMode(LED_PIN, OUTPUT);
  ledOff();
  DEBUG_PRINT("LED indicator initialized on GPIO");
  DEBUG_PRINTLN(LED_PIN);
  
  #if defined(ESP32)
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledTimer = timerBegin(1000000);
      timerAttachInterrupt(ledTimer, &ledTimerCallback);
      timerAlarm(ledTimer, 100000, true, 0);
      DEBUG_PRINTLN("ESP32 hardware timer initialized (Core 3.x, 100ms interval)");
    #else
      ledTimer = timerBegin(0, 80, true);
      timerAttachInterrupt(ledTimer, &ledTimerCallback, true);
      timerAlarmWrite(ledTimer, 100000, true);
      timerAlarmEnable(ledTimer);
      DEBUG_PRINTLN("ESP32 hardware timer initialized (Core 2.x, 100ms interval)");
    #endif
    
  #elif defined(ESP8266)
    timer1_isr_init();
    timer1_attachInterrupt(onTimer1ISR);
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
    timer1_write(500000);
    DEBUG_PRINTLN("ESP8266 Timer1 initialized (100ms interval)");
  #endif

  setLedPattern(LED_WIFI_DISCONNECTED);
}

void setLedPattern(LedPattern pattern) {
  if (pattern == currentPattern) {
    return;
  }
  
  #if defined(ESP32)
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      timerStop(ledTimer);
    #else
      timerAlarmDisable(ledTimer);
    #endif
    
    portENTER_CRITICAL(&ledTimerMux);
    flashCount = 0;
    ledState = false;
    currentPattern = pattern;
    portEXIT_CRITICAL(&ledTimerMux);
    
    ledOff();
    
    if (pattern != LED_OFF) {
      #if ESP_ARDUINO_VERSION_MAJOR >= 3
        timerStart(ledTimer);
      #else
        timerAlarmEnable(ledTimer);
      #endif
    }
    
  #elif defined(ESP8266)
    noInterrupts();
    flashCount = 0;
    ledState = false;
    currentPattern = pattern;
    interrupts();
    
    ledOff();
  #endif
}

void updateLedStatus() {
  if (ledOverrideActive) {
    return;
  }
  
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

void setConnectivityState(bool wifiConnected, bool mqttConnected) {
  currentConnectivity.wifiConnected = wifiConnected;
  currentConnectivity.mqttConnected = mqttConnected;
  updateLedStatus();
}

void beginLedOverride(LedPattern pattern) {
  ledOverrideActive = true;
  ledOverridePattern = pattern;
  setLedPattern(pattern);
}

void endLedOverride() {
  ledOverrideActive = false;
  updateLedStatus();
}
