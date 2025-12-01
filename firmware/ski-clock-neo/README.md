# Ski Clock Neo - Firmware

ESP32/ESP8266 firmware for NeoPixel LED matrix clock displays with secure OTA updates, MQTT telemetry, and multi-panel support.

## Features

- **Multi-Panel Display**: Supports variable panel counts per row (e.g., 3 panels on row 1, 4 on row 2)
- **16x16 NeoPixel Matrices**: Custom 5x7 pixel font with automatic spacing
- **WiFi Management**: AutoConnect library with captive portal for easy network setup
- **Secure OTA Updates**: HTTPS with API key authentication, automatic version checking
- **MQTT Telemetry**: Heartbeats, event logging, display snapshots, remote commands
- **RTC Support**: DS3231 for instant time on boot with NTP sync fallback
- **Temperature Sensor**: DS18B20 with non-blocking reads
- **Button Timer Mode**: Countdown/stopwatch with visual feedback
- **Event Logging**: Ring buffer with MQTT publishing for device health monitoring

## Supported Platforms

| Platform ID | Chip | Board Examples |
|-------------|------|----------------|
| esp32 | ESP32 | ESP32 DevKit, ESP32-WROOM |
| esp32c3 | ESP32-C3 | ESP32-C3 SuperMini, Seeed XIAO ESP32C3 |
| esp32s3 | ESP32-S3 | ESP32-S3 DevKit |
| esp12f | ESP8266 | ESP-12F modules |
| esp01 | ESP8266 | ESP-01, ESP-01S |
| d1mini | ESP8266 | Wemos D1 Mini, NodeMCU |

## Hardware Requirements

### Core Components
- ESP32 or ESP8266 microcontroller
- 16x16 WS2812B NeoPixel panels (1-8 per row)
- 5V power supply (sized for LED count)

### Optional Components
- DS3231 RTC module (for instant time on boot)
- DS18B20 temperature sensor
- Momentary push button (for timer mode)

### Default Pin Configuration

| Component | GPIO | Notes |
|-----------|------|-------|
| Display Row 0 | 4 | NeoPixel data |
| Display Row 1 | 3 | NeoPixel data |
| Button | 0 | Boot button (use external for production) |
| Temperature | 2 | DS18B20 OneWire |
| RTC SDA | 5 | I2C data |
| RTC SCL | 6 | I2C clock |

> **Note**: ESP32-C3 requires explicit I2C pin configuration to avoid conflicts with GPIO 8/9.

## Project Structure

```
ski-clock-neo/
├── ski-clock-neo.ino          # Main sketch (Arduino IDE entry point)
├── ski-clock-neo_config.h     # User-tunable configuration
└── src/                        # Source files (Arduino compiles recursively)
    ├── core/                   # Shared infrastructure
    │   ├── timer_helpers.h/.cpp   # Platform-abstracted timing (FreeRTOS/TickTwo)
    │   ├── event_log.h/.cpp       # MQTT event logging with ring buffer
    │   ├── led_indicator.h/.cpp   # Connectivity status LED
    │   ├── device_info.h/.cpp     # Device ID and platform detection
    │   └── debug.h                # Conditional debug macros
    ├── data/                   # Sensor and time data providers
    │   ├── data_time.h/.cpp       # RTC/NTP time management
    │   ├── data_temperature.h/.cpp # DS18B20 sensor
    │   └── data_button.h/.cpp     # Button input with debouncing
    ├── connectivity/           # Network and updates
    │   ├── mqtt_client.h/.cpp     # MQTT pub/sub and commands
    │   ├── ota_update.h/.cpp      # OTA firmware updates
    │   └── wifi_config.h          # AutoConnect WiFi management
    └── display/                # Display rendering
        ├── display_core.h/.cpp    # Hardware-agnostic buffer management
        ├── display_controller.h/.cpp # Content scheduling and modes
        ├── neopixel_render.h/.cpp # NeoPixel-specific rendering
        └── font_5x7.h             # Bitmap font data
```

> **Note**: The `src/` folder structure allows Arduino to compile all `.cpp` files recursively.

### Include Path Conventions

| From | Include Path |
|------|-------------|
| .ino file | `#include "src/core/timer_helpers.h"` |
| Files in src/ | `#include "core/timer_helpers.h"` |
| Config from src/ | `#include "../../ski-clock-neo_config.h"`

## Configuration

Edit `ski-clock-neo_config.h` to customize your setup:

```cpp
// Hardware pins
#define BUTTON_PIN      0       // Button GPIO
#define TEMPERATURE_PIN 2       // DS18B20 GPIO
#define RTC_SDA_PIN     5       // I2C data
#define RTC_SCL_PIN     6       // I2C clock

// Display dimensions
#define PANEL_WIDTH     16      // Pixels per panel
#define PANEL_HEIGHT    16      // Pixels per panel
#define DISPLAY_ROWS    2       // Number of physical rows

// Panels per row (supports variable counts)
static const uint8_t PANELS_PER_ROW[DISPLAY_ROWS] = {3, 3};

// Display color (RGB)
#define DISPLAY_COLOR_R 255
#define DISPLAY_COLOR_G 0
#define DISPLAY_COLOR_B 0
#define BRIGHTNESS      10      // 0-255

// Display data pins (one per row)
static const uint8_t DISPLAY_PINS[DISPLAY_ROWS] = {4, 3};
```

## Dependencies

Install these libraries via Arduino Library Manager or PlatformIO:

| Library | Purpose |
|---------|---------|
| [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) | LED matrix control |
| [RTClib](https://github.com/adafruit/RTClib) | DS3231 RTC support |
| [AutoConnect](https://github.com/Hieromon/AutoConnect) | WiFi management with captive portal |
| [PubSubClient](https://github.com/knolleary/pubsublient) | MQTT client (with TLS) |
| [OneWire](https://github.com/PaulStoffregen/OneWire) | DS18B20 communication |
| [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) | Temperature sensor |
| [TickTwo](https://github.com/sstaub/TickTwo) | ESP8266 timer library |

### Board Packages

- **ESP32**: [espressif/arduino-esp32](https://github.com/espressif/arduino-esp32)
- **ESP8266**: [esp8266/Arduino](https://github.com/esp8266/Arduino)

## Building

### Arduino IDE

1. Install ESP32 or ESP8266 board package
2. Install required libraries
3. Open `ski-clock-neo.ino`
4. Select your board from Tools > Board
5. Compile and upload

### PlatformIO

Create `platformio.ini` in the project root:

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    adafruit/Adafruit NeoPixel
    adafruit/RTClib
    hieromon/AutoConnect
    knolleary/PubSubClient
    paulstoffregen/OneWire
    milesburton/DallasTemperature

[env:esp8266]
platform = espressif8266
board = nodemcuv2
framework = arduino
lib_deps = 
    adafruit/Adafruit NeoPixel
    adafruit/RTClib
    hieromon/AutoConnect
    knolleary/PubSubClient
    paulstoffregen/OneWire
    milesburton/DallasTemperature
    sstaub/TickTwo
```

### GitHub Actions CI/CD (Optional)

You can create GitHub Actions workflows to automate builds:
1. Generate timestamp-based versions (`YYYY.MM.DD.BUILD`)
2. Compile firmware for all platform variants
3. Inject server URL and API keys at build time
4. Upload binaries to the dashboard server's `/api/upload` endpoint

See the dashboard's README for upload API details.

## WiFi Setup

On first boot (or when no network is available):

1. Device creates AP: `SkiClock-Setup` (password: `configure`)
2. Connect to this network
3. Captive portal opens automatically
4. Select your WiFi network and enter credentials
5. Device connects and saves settings

The portal remains accessible for adding/managing networks.

## OTA Updates

The firmware automatically:
- Checks for updates every hour
- Downloads and installs when newer version available
- Retries every 5 minutes on failure
- Reports progress via MQTT
- Supports rollback on ESP32 (dual partition)

### Server Requirements

OTA updates require a compatible server (see `dashboard/` folder) providing:
- `GET /api/version?platform=<platform>` - Version check
- `GET /api/firmware/<platform>` - Firmware download
- API key authentication via `X-API-Key` header

## MQTT Topics

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `skiclock/heartbeat/<deviceId>` | Publish | Device status (60s interval) |
| `skiclock/snapshot/<deviceId>` | Publish | Display snapshot (hourly) |
| `skiclock/events/<deviceId>` | Publish | Event log entries |
| `skiclock/ota/<deviceId>` | Publish | OTA progress updates |
| `skiclock/command/<deviceId>` | Subscribe | Remote commands |

### Supported Commands

Send to `skiclock/command/<deviceId>`:
- `restart` - Reboot device
- `rollback` - Revert to previous firmware (ESP32 only)
- `snapshot` - Request immediate display snapshot

## Display Modes

### Normal Mode
- Row 0: Alternates time/date every 4 seconds
- Row 1: Temperature reading

### Timer Mode (Button Controlled)
1. **Press** → Countdown (3, 2, 1)
2. **Release** → Timer starts (counts up)
3. **Press again** → Timer stops, flashes result
4. After 1 minute, auto-returns to normal mode

## Event Types

The event logging system tracks:

| Category | Events |
|----------|--------|
| System | boot, heartbeat, low_heap_warning |
| WiFi | wifi_connect, wifi_disconnect, wifi_rssi_low |
| MQTT | mqtt_connect, mqtt_disconnect |
| Temperature | temperature_read, temperature_error |
| RTC/Time | rtc_initialized, ntp_sync_success, rtc_synced_from_ntp |
| User Input | button_press, button_release |
| Display | display_mode_change |

## Architecture Notes

### Platform Abstraction

The `timer_helpers` library provides unified timing across platforms:
- **ESP32**: FreeRTOS tasks and Ticker callbacks
- **ESP8266**: TickTwo library with manual `updateTimers()` in loop

### Display Architecture

The display system is hardware-agnostic:
- `display_core`: Manages bit-packed buffer and text content
- `display_controller`: Handles content scheduling and display modes
- `neopixel_render`: Hardware-specific rendering (rotation, serpentine wiring)

This design supports future migration to HUB75 panels.

### Thread Safety

ESP32 builds use critical sections for thread-safe access to shared state between FreeRTOS tasks, ensuring zero race conditions during concurrent display updates.

## Troubleshooting

### No Display Output
- Verify NeoPixel data pin matches `DISPLAY_PINS` config
- Check power supply capacity (60mA per LED at full white)
- Ensure ground is shared between MCU and LED strip

### WiFi Connection Issues
- Factory reset by holding button during boot
- Check signal strength (device logs RSSI warnings)
- Verify 2.4GHz network (5GHz not supported)

### OTA Update Failures
- Verify server URL and API key
- Check available flash space (ESP8266 needs OTA partition)
- Review MQTT logs for progress/error messages

### RTC Not Detected
- Verify I2C wiring (SDA/SCL)
- Check pin configuration for ESP32-C3 variants
- Device falls back to NTP-only mode if RTC unavailable

## License

See repository root for license information.
