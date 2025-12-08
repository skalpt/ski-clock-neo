#ifndef SKI_CLOCK_NEO_CONFIG_H
#define SKI_CLOCK_NEO_CONFIG_H

#include <Arduino.h>

// ============================================================================
// LOCAL BUILD CREDENTIALS (Arduino IDE only)
// ============================================================================
// Uncomment and fill in these values when compiling directly in Arduino IDE.
// GitHub Actions passes these as build flags, so they're not needed there.
//
// WARNING: Never commit this file with real credentials filled in!
//          Keep these commented out in version control.
// ============================================================================

// #define MQTT_HOST         "your-broker.hivemq.cloud"
// #define MQTT_USERNAME     "your-mqtt-username"
// #define MQTT_PASSWORD     "your-mqtt-password"
// #define UPDATE_SERVER_URL "https://your-update-server.com"
// #define DOWNLOAD_API_KEY  "your-api-key"
// #define FIRMWARE_VERSION  "2025.01.01.1"

// Environment scope for MQTT topics (dev or prod)
// Default is "dev" - devices can be promoted to "prod" via MQTT config at runtime.
// The environment is stored as a single-byte enum in EEPROM/NVS:
//   0 = default (dev), 1 = dev, 2 = prod
// To change at runtime: send {"environment": "prod"} to norrtek-iot/{env}/config/{device_id}
//
// PENDING_ENV_SCOPE: Optional compile-time flag to set initial environment on first boot.
// Use -DPENDING_ENV_SCOPE=2 in build flags to provision devices directly to prod.
// This value is written to storage on first boot only, then MQTT can override it.
// Values: 0 or undefined = dev (default), 1 = dev, 2 = prod
#ifndef PENDING_ENV_SCOPE
  #define PENDING_ENV_SCOPE 0
#endif

// Board type - uncomment ONE of these to match your hardware:
// #define BOARD_ESP32        // Generic ESP32
// #define BOARD_ESP32C3      // ESP32-C3
// #define BOARD_ESP32S3      // ESP32-S3
// #define BOARD_ESP12F       // ESP-12F module
// #define BOARD_ESP01        // ESP-01 module
// #define BOARD_WEMOS_D1MINI // Wemos D1 Mini

// ============================================================================
// PRODUCT CONFIGURATION
// ============================================================================

// Product identification
#define PRODUCT_NAME "ski-clock-neo"

// Hardware pin configuration
#define DISPLAY_PIN_ROW0 4       // Display row 0 on GPIO4
#define DISPLAY_PIN_ROW1 3       // Display row 1 on GPIO3
#define RTC_SDA_PIN      5       // RTC I2C data pin on GPIO5
#define RTC_SCL_PIN      6       // RTC I2C clock pin on GPIO 6
#define TEMPERATURE_PIN  2       // DS18B20 temperature sensor on GPIO2
#define TEMPERATURE_OFFSET 0.0  // Temperature calibration offset (degrees C)
#define BUTTON_PIN       0       // Button on GPIO0 (CAUTION: boot button)

// Display dimension configuration
#define PANEL_WIDTH     16      // Width of each panel in pixels
#define PANEL_HEIGHT    16      // Height of each panel in pixels
#define DISPLAY_ROWS    2       // Number of physical display rows

// Display color configuration (RGB)
#define DISPLAY_COLOR_R 255     // Red component (0-255)
#define DISPLAY_COLOR_G 0       // Green component (0-255)
#define DISPLAY_COLOR_B 0       // Blue component (0-255)
#define BRIGHTNESS      255     // 0-255 (keeping dim for development)

// Activity pixel - blinks bottom-right pixel of each row every second
#define ACTIVITY_PIXEL_ENABLED true

// Per-row panel counts (allows different widths per row)
// Example: Row 0 = 3 panels (48px wide), Row 1 = 4 panels (64px wide)
static const uint8_t PANELS_PER_ROW[DISPLAY_ROWS] = {4, 4};

// Display pin configuration (one GPIO per row)
static const uint8_t DISPLAY_PINS[DISPLAY_ROWS] = {DISPLAY_PIN_ROW0, DISPLAY_PIN_ROW1};

// Helper macros for calculating row dimensions
#define ROW_WIDTH(row) (PANELS_PER_ROW[row] * PANEL_WIDTH)
#define ROW_PIXELS(row) (ROW_WIDTH(row) * PANEL_HEIGHT)

// Calculate maximum row width (for buffer sizing)
inline uint8_t getMaxPanelsPerRow() {
  uint8_t maxPanels = 0;
  for (uint8_t i = 0; i < DISPLAY_ROWS; i++) {
    if (PANELS_PER_ROW[i] > maxPanels) maxPanels = PANELS_PER_ROW[i];
  }
  return maxPanels;
}

// Calculate total pixels across all rows
inline uint16_t getTotalPixels() {
  uint16_t total = 0;
  for (uint8_t i = 0; i < DISPLAY_ROWS; i++) {
    total += ROW_PIXELS(i);
  }
  return total;
}

#endif
