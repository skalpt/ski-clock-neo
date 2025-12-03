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
#define TEMPERATURE_OFFSET -2.0  // Temperature calibration offset (degrees C)
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

// Activity LED - blinks bottom-right pixel of each row every second
#define ACTIVITY_LED_ENABLED true

// Per-row panel counts (allows different widths per row)
// Example: Row 0 = 3 panels (48px wide), Row 1 = 4 panels (64px wide)
static const uint8_t PANELS_PER_ROW[DISPLAY_ROWS] = {3, 3};

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
