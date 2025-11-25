#ifndef SKI_CLOCK_NEO_CONFIG_H
#define SKI_CLOCK_NEO_CONFIG_H

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

// Display pin mapping (declared here, defined in .ino)
extern const uint8_t DISPLAY_PINS[DISPLAY_ROWS];

#endif
