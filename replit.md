# Ski Clock Neo - Project Documentation

## Overview
Ski Clock Neo integrates Arduino firmware for NeoPixel LED matrix displays with a custom, secure firmware update server. The system is designed for ESP32/ESP8266 hardware, displaying dynamic content on a 16x16 NeoPixel matrix. A Python Flask dashboard manages and distributes firmware updates, supporting various ESP board types. The project focuses on automatic, secure, and robust over-the-air (OTA) updates, enabling seamless deployment and management of embedded devices with high reliability and ease of migration to self-hosted infrastructure.

## User Preferences
- Target platform: ESP32/ESP8266 (not standard Arduino)
- Multi-panel support is intentional (not a bug)
- Private repository (no public releases)
- Configurable server URL for future migration

## System Architecture
The project consists of two main components:

1.  **Firmware**: Embedded C++ for ESP32/ESP8266 microcontrollers driving a 16x16 NeoPixel LED matrix.
    *   **Features**: Custom 5x7 pixel font with diagonal smoothing, multi-panel support, freeze-proof LED status indicator with hardware interrupt timers, advanced WiFi management via `AutoConnect` (multi-network credentials, captive portal, background reconnection), secure non-blocking OTA updates, and real-time MQTT heartbeat monitoring.
    *   **OTA Implementation**: Utilizes a custom update server, API key authentication, configurable server URL, HTTPS with certificate validation, and non-blocking chunked HTTP reads with yield() calls to prevent UI freezing during version checks.
    *   **MQTT Monitoring**: Publishes device heartbeats every 60 seconds to HiveMQ Cloud broker (device ID, board type, firmware version, uptime, WiFi RSSI, free heap). Subscribes to version update notifications for instant OTA trigger. Uses TLS encryption without certificate validation (works offline, no NTP required). WiFi event-driven lifecycle management with automatic connect/disconnect. Broker reconnection with 5-second retry interval for network resilience.
    *   **Timing**: Hardware interrupt timers (hw_timer_t for ESP32, Timer1 for ESP8266) for LED indicators ensure guaranteed execution even during blocking operations. NeoPixel updates use FreeRTOS tasks on ESP32 (high priority on C3, Core 1 on dual-core) for smooth rendering during network operations; ESP8266 uses software tickers. OTA checks use software tickers. MQTT publishes at 60-second intervals with automatic reconnection.

2.  **Dashboard Server**: A Python Flask application for firmware distribution and device management.
    *   **Features**: API-based firmware distribution supporting multiple platforms (ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, Wemos D1 Mini), upload/download endpoints with API key authentication, platform aliasing (e.g., ESP8266 to ESP-12F), SHA256 checksums, real-time device monitoring via MQTT, PostgreSQL database for persistent device tracking, and interactive web dashboard.
    *   **MQTT Integration**: Background MQTT subscriber (paho-mqtt) connects to HiveMQ Cloud via TLS (port 8883), subscribes to heartbeat messages from all devices, saves devices to PostgreSQL database on first heartbeat, updates last_seen timestamp on subsequent heartbeats, and exposes live device data via REST API. Web UI auto-refreshes every 10 seconds to display connected devices with online/offline status (15-minute threshold), uptime, WiFi signal strength, and firmware versions.
    *   **Device Management**: PostgreSQL database stores device registry (device_id, board_type, firmware_version, first_seen, last_seen, uptime, rssi, free_heap). Dashboard shows online/offline status with visual distinction (gray cards for offline), minutes since last seen, total/online/offline counts, and delete button per device for decommissioning. Devices offline for 15+ minutes are marked as offline.
    *   **Deployment**: VM deployment (always-on) ensures consistent state across requests and maintains persistent MQTT connections. Fully automatic CI/CD via GitHub Actions builds firmware for various board variants, generates timestamp-based versions (`year.month.day.buildnum`), injects MQTT credentials, and uploads binaries and configuration to the dashboard.
    *   **System Design Choices**: Emphasizes automated versioning, secure communication, and graceful fallback for optional services like Object Storage. Secrets are managed securely in GitHub and Replit, with GitHub Actions injecting configuration (including MQTT credentials) into the dashboard. VM deployment prevents multi-instance state synchronization issues and ensures MQTT connections remain active.

## Build Configuration

### Optimized Partition Schemes
Since the project doesn't use SPIFFS, all boards use minimal SPIFFS partitions to maximize app space:
- **ESP32/C3/S3**: `min_spiffs` partition (1.9MB app + 190KB SPIFFS)
- **ESP-12F/D1 Mini**: `4M1M` partition (1MB app + 1MB OTA + 1MB SPIFFS)
- **ESP-01**: `1M` partition (1MB chip, no OTA possible)

### Debug Logging System
Debug logging is **enabled** by default for development and troubleshooting:
- **Status**: `#define DEBUG_LOGGING` is enabled in `firmware/ski-clock-neo/debug.h`
- **Macros**: All code uses `DEBUG_PRINT()` and `DEBUG_PRINTLN()` for serial output
- **Output**: Serial messages help with WiFi connection, OTA updates, and system diagnostics
- **To disable**: Comment out the `#define DEBUG_LOGGING` line to remove debug output

## External Dependencies
-   **Firmware Libraries**:
    -   **Adafruit_NeoPixel**: For controlling NeoPixel LED matrices.
    -   **AutoConnect**: Arduino library for advanced WiFi management and captive portal.
    -   **PubSubClient**: MQTT client library for ESP32/ESP8266 with TLS support.
    -   **ESP32 Arduino Core 2.0.14**: Specific version required for AutoConnect compatibility.
    -   **ESP8266 WiFi libraries**: Built-in for ESP8266 devices.
-   **Dashboard Dependencies**:
    -   **Flask**: Web framework for REST API and UI serving.
    -   **paho-mqtt**: Python MQTT client for subscribing to device heartbeats.
    -   **Replit Object Storage** (Optional): For persistent storage of firmware binaries and version metadata, with graceful fallback to local filesystem.
-   **Cloud Services**:
    -   **HiveMQ Cloud Serverless**: MQTT broker for real-time device monitoring (free tier: 100 devices, 10GB/month). Uses TLS (port 8883) with setInsecure() for simplified connection (no cert validation).
    -   **GitHub Actions**: CI/CD platform for automated firmware builds, versioning, and deployment to the dashboard.
## Recent Changes

- **2025-11-20**: Implemented PostgreSQL database for persistent device tracking with first_seen/last_seen timestamps
- **2025-11-20**: Added device management API: DELETE /api/devices/<device_id> endpoint for removing decommissioned devices
- **2025-11-20**: Enhanced dashboard UI with online/offline status indicators, stats bar (Total/Online/Offline counts), and delete buttons
- **2025-11-20**: MQTT subscriber now saves devices to database on first heartbeat and updates last_seen on subsequent heartbeats
- **2025-11-20**: Fixed MQTT client ID collision between dev/prod by generating unique IDs (SkiClockDashboard-{env}-{uuid})
- **2025-11-20**: Implemented WiFi event handlers (ARDUINO_EVENT_WIFI_STA_GOT_IP/DISCONNECTED) for automatic MQTT lifecycle management
- **2025-11-20**: Added disconnectMQTT() function with proper heartbeat ticker cleanup on WiFi loss
- **2025-11-20**: Implemented broker reconnection logic (5s retry) for network resilience while WiFi stays up
- **2025-11-20**: Simplified MQTT TLS to use setInsecure() - no certificate validation or NTP sync required
- **2025-11-20**: Removed certificates.h (no longer needed - OTA uses setInsecure(), MQTT uses setInsecure())
- **2025-11-19**: Implemented MQTT heartbeat monitoring system with HiveMQ Cloud integration
- **2025-11-19**: Added real-time device status dashboard with auto-refresh UI (10-second intervals)
- **2025-11-19**: Created mqtt_client.h for firmware with TLS encryption (no cert validation for offline support)
- **2025-11-19**: Implemented paho-mqtt subscriber in dashboard with background thread for heartbeat processing
- **2025-11-19**: Added /api/devices endpoint to expose live device status from MQTT messages
- **2025-11-19**: Injected MQTT credentials (MQTT_HOST, MQTT_USERNAME, MQTT_PASSWORD) into firmware builds via GitHub Actions
- **2025-11-19**: Device heartbeats include: device ID, board type, version, uptime, RSSI, free heap memory
- **2025-11-19**: Converted NeoPixel updates to FreeRTOS tasks on ESP32 (Core 1 for dual-core, high priority for C3) to prevent freezing during OTA checks
- **2025-11-19**: Configured retainPortal to keep captive portal AP accessible after WiFi connection
- **2025-11-19**: Enabled USB CDC On Boot for ESP32-C3/S3 builds to fix Serial output
- **2025-11-19**: Fixed getLatestVersion() buffer size limit (200→1024 bytes) to handle full JSON responses
- **2025-11-19**: Fixed OTA download WiFiClient scope bug causing "connection lost" errors during firmware downloads
- **2025-11-19**: Switched deployment from autoscale to VM to fix multi-worker state synchronization issues
- **2025-11-19**: Removed aggressive compiler optimizations (LTO, -Os, etc.) - min_spiffs partition provides sufficient space
- **2025-11-19**: Enabled debug logging by default for development and troubleshooting
- **2025-11-19**: Implemented freeze-proof clock system with hardware interrupt timers for LED status
- **2025-11-19**: Made OTA version checks non-blocking with chunked HTTP reads and yield() calls
- **2025-11-19**: LED now uses hw_timer_t (ESP32) and Timer1 (ESP8266) for true interrupt-driven operation
- **2025-11-19**: Optimized partition schemes - ESP32 boards use min_spiffs (1.9MB app), ESP8266 boards use 4M1M (enables OTA)
- **2025-11-19**: Fixed ESP8266 boards - changed from 4M3M (no OTA) to 4M1M (OTA enabled)
- **2025-11-19**: Fixed ESP32-S3 inconsistent GPIO register types (bank 0 direct uint32_t, bank 1 with .val accessor)
- **2025-11-19**: ESP32-C3 LED_PIN overridden to GPIO8 (board-specific requirement)
- **2025-11-19**: Separated ESP32-C3/S3 preprocessor directives (C3 has no out1 registers, S3 needs dual banks)
- **2025-11-19**: Fixed ESP32-C3/S3 fast GPIO with dual GPIO bank support (0-31, 32-48) for ~10-20 MHz toggle speed
- **2025-11-19**: ESP32-S3 high-pin support for LED_BUILTIN on GPIO48 using GPIO.out1_w1ts.val
- **2025-11-19**: Added LED_BUILTIN fallback to GPIO2 for boards without native definition (generic ESP8266)
- **2025-11-19**: Fixed LED GPIO macro name collision (LED_OFF enum vs LED_GPIO_OFF state)
- **2025-11-19**: **PRODUCTION-READY**: Multi-platform OTA firmware system with 6 board variants
- **2025-11-19**: Implemented interrupt-driven LED indicators with fast port manipulation and LED_BUILTIN constant
- **2025-11-19**: Fixed ESP32/ESP8266 inverted LED pin logic (HIGH=off, LOW=on)
- **2025-11-19**: Converted to software ticker architecture for NeoPixels and OTA (no more flags in loop)
- **2025-11-19**: OTA checks now self-scheduling via software ticker (30s initial, 1h recurring)
- **2025-11-19**: Removed all blocking delays from setup() and loop() - captive portal responsive immediately
- **2025-11-19**: Increased NeoPixel brightness from 1 to 32 for better visibility
- **2025-11-19**: Migrated firmware storage to Replit Object Storage for persistence across deployments
- **2025-11-19**: Added graceful fallback to local filesystem when Object Storage not configured
