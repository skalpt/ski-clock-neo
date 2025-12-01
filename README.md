# Ski Clock Neo

ESP32/ESP8266 firmware with NeoPixel LED matrix displays and a custom, secure firmware update server. This system enables dynamic content display on 16x16 NeoPixel matrices and facilitates automatic, secure over-the-air (OTA) updates via a Python Flask dashboard.

## Overview

The project emphasizes robust deployment, simplified embedded device management, and easy migration to self-hosted infrastructure, aiming for high reliability and seamless updates.

## Project Structure

```
ski-clock-neo/
├── firmware/ski-clock-neo/     # ESP32/ESP8266 Arduino firmware
│   ├── core/                   # Shared infrastructure (timers, events, LED)
│   ├── data/                   # Sensor/time data providers
│   ├── connectivity/           # MQTT, OTA, WiFi management
│   └── display/                # Display buffer and rendering
└── dashboard/                  # Flask web application
    ├── app.py                  # Main server
    ├── models.py               # Database models
    └── mqtt_subscriber.py      # MQTT background processor
```

See individual README files in each directory for detailed documentation:
- [Firmware Documentation](firmware/ski-clock-neo/README.md)
- [Dashboard Documentation](dashboard/README.md)

## System Architecture

### Firmware (Embedded C++ for ESP32/ESP8266)

**Core Features:**
- Drives 16x16 NeoPixel matrices with custom 5x7 pixel font
- Supports multi-panel setups with variable panels per row
- Freeze-proof LED status indicator using hardware interrupt timers
- WiFi management via AutoConnect with captive portal
- Secure non-blocking OTA updates with API key authentication
- FreeRTOS tasks on ESP32 for smooth rendering

**Display Architecture:**
- Hardware-agnostic `display_core` manages bit-packed buffer and text content
- `neopixel_render` handles pixel transformations (rotation, serpentine wiring)
- Supports future migration to HUB75 panels

**Connectivity:**
- MQTT heartbeats to HiveMQ Cloud every 60 seconds
- Display snapshot publishing (hourly + on-demand)
- Remote commands: restart, rollback, snapshot
- Real-time OTA progress reporting

**Sensors & Inputs:**
- DS3231 RTC for instant time on boot with NTP sync fallback
- DS18B20 temperature sensor with non-blocking reads
- Button-controlled timer mode (countdown/stopwatch)

**Event Logging:**
- Ring buffer stores device events with timestamps
- Event types: system, connectivity, temperature, RTC/time, user input, display

### Dashboard Server (Python Flask)

**Core Features:**
- Firmware distribution API for 6 platform variants
- Upload with API key authentication and SHA256 checksums
- PostgreSQL persistence for devices, firmware, and events
- Interactive web dashboard with light/dark mode

**Device Monitoring:**
- MQTT subscriber processes heartbeats and events
- Online/degraded/offline status detection
- Display snapshot visualization with color accuracy
- Live events feed with auto-refresh

**Security:**
- Session-based authentication for dashboard
- API key authentication for device endpoints
- Role-Based Access Control (RBAC)
- Fail-closed production configuration validation

**Additional Features:**
- Browser-based firmware flashing via ESP Web Tools
- Firmware version pinning per device
- Production sync for development environments
- OTA update tracking with progress visualization

## Supported Platforms

| Platform | Chip | Board Examples |
|----------|------|----------------|
| esp32 | ESP32 | ESP32 DevKit, ESP32-WROOM |
| esp32c3 | ESP32-C3 | ESP32-C3 SuperMini, XIAO |
| esp32s3 | ESP32-S3 | ESP32-S3 DevKit |
| esp12f | ESP8266 | ESP-12F modules |
| esp01 | ESP8266 | ESP-01, ESP-01S |
| d1mini | ESP8266 | Wemos D1 Mini |

## Quick Start

### 1. Deploy the Dashboard

The dashboard runs on Replit at port 5000:

1. Configure secrets in Replit:
   - `DATABASE_URL` - PostgreSQL connection
   - `UPLOAD_API_KEY` - For CI/CD uploads
   - `DOWNLOAD_API_KEY` - For device downloads
   - `MQTT_HOST`, `MQTT_USERNAME`, `MQTT_PASSWORD` - MQTT broker

2. Click **Publish** to get a permanent URL

### 2. Build Firmware

**Option A: Manual Build (Arduino IDE/PlatformIO)**
1. Open `firmware/ski-clock-neo/ski-clock-neo.ino`
2. Configure your board and upload
3. Use dashboard's upload API to add firmware

**Option B: CI/CD with GitHub Actions (Manual Setup Required)**

Create your own GitHub Actions workflow (`.github/workflows/build.yml`) to:
1. Generate version: `YYYY.MM.DD.BUILD`
2. Compile for all platforms using arduino-cli or PlatformIO
3. Inject server URL and API key via build flags
4. Upload binaries via the dashboard's `/api/upload` endpoint

See [Dashboard README](dashboard/README.md) for the upload API format.

### 3. Flash Device

**Option A: USB (ESP Web Tools)**
- Open dashboard, click "Flash" for your platform
- Connect device via USB
- Browser handles flashing

**Option B: OTA**
- Device checks for updates hourly
- Downloads and installs automatically
- Reports progress via MQTT

### 4. WiFi Setup

On first boot:
1. Device creates AP: `SkiClock-Setup` (password: `configure`)
2. Connect and enter WiFi credentials
3. Device connects and starts sending heartbeats

## External Dependencies

### Firmware Libraries
- Adafruit_NeoPixel - LED matrix control
- RTClib (Adafruit) - DS3231 RTC support
- AutoConnect - WiFi management
- PubSubClient - MQTT with TLS
- OneWire/DallasTemperature - Temperature sensor
- TickTwo - ESP8266 timers

### Dashboard
- Flask - Web framework
- paho-mqtt - MQTT client
- PostgreSQL - Database
- Flask-SQLAlchemy - ORM

### Cloud Services
- HiveMQ Cloud Serverless - MQTT broker
- GitHub Actions - CI/CD pipeline

## Display Snapshot Format

Supports variable panel counts per row:

```json
{
  "rows": [
    {"text": "12:34", "cols": 3, "width": 48, "height": 16, "mono": "base64..."},
    {"text": "68°F", "cols": 4, "width": 64, "height": 16, "mono": "base64..."}
  ]
}
```

## User Preferences

- Target platform: ESP32/ESP8266 (not standard Arduino)
- Multi-panel support is intentional
- Private repository (no public releases)
- Configurable server URL for future migration

## License

See repository for license information.
