// Hardware pin configuration
#define BUTTON_PIN 0            // Button on GPIO0 (CAUTION: boot button)
#define TEMP_SENSOR_PIN 2       // DS18B20 temperature sensor on GPIO2
#define RTC_SDA_PIN 5           // RTC I2C data pin on GPIO5
#define RTC_SCL_PIN 6           // RTC I2C clock pin on GPIO 6
#define RTC_SQW_PIN 7           // RTC square wave pin (for hardware interrupts) on GPIO7

// Display configuration
#define DISPLAY_ROWS    2       // Number of physical display rows
#define PANELS_PER_ROW  3       // Number of panels per row
#define PANEL_WIDTH     16      // Width of each panel in pixels
#define PANEL_HEIGHT    16      // Height of each panel in pixels
#define BRIGHTNESS      10      // 0-255 (keeping dim for development)
const uint8_t DISPLAY_PINS[DISPLAY_ROWS] = {4, 3};  // Row 1: GPIO4, Row 2: GPIO3

// Includes
#include "debug.h"
#include "led_indicator.h"
#include "display.h"
#include "display_controller.h"
#include "data_temperature.h"  // For updateTemperatureData() on ESP8266
#include "button.h"
#include "wifi_config.h"
#include "mqtt_client.h"

void setup() {
  // Initialise serial (only if debug logging enabled)
  DEBUG_BEGIN(115200);
  DEBUG_PRINTLN("\n\n===========================================");
  DEBUG_PRINTLN("Ski Clock Neo - Starting Up");
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINT("Firmware version: ");
  DEBUG_PRINTLN(FIRMWARE_VERSION);

  // Initialize onboard LED indicator
  initLedIndicator();

  // Initialize display (hardware only - no controller yet)
  initDisplay();
  
  // Initialize display controller (with time/temp data libraries)
  // This is called separately to pass the temperature sensor pin
  initDisplayController(TEMP_SENSOR_PIN);

  // Initialize button
  initButton();

  // Initialise WiFi
  initWiFi();
  
  // Initialize MQTT system
  initMQTT();
    
  DEBUG_PRINTLN("===========================================");
  DEBUG_PRINTLN("Setup complete - entering main loop");
  DEBUG_PRINTLN("===========================================\n");
}

void loop() {
  // Handle WiFi tasks (config portal or reconnection)
  updateWiFi();

  // Handle MQTT updates (subscriptions and version requests)
  updateMQTT();
  
  // Update button state (debouncing and callbacks)
  updateButton();
  
  // Update software tickers (ESP8266 only - loop-driven, non-ISR, WiFi-safe)
  // ESP32 uses FreeRTOS tasks, so these are no-ops
  #if defined(ESP8266)
    updateDisplayController();  // Time/date toggle ticker
    updateTemperatureData();    // Temperature poll/read tickers
  #endif
}
