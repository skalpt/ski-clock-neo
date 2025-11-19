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
    *   **Features**: Custom 5x7 pixel font with diagonal smoothing, multi-panel support, freeze-proof LED status indicator with hardware interrupt timers, advanced WiFi management via `AutoConnect` (multi-network credentials, captive portal, background reconnection), and secure non-blocking OTA updates.
    *   **OTA Implementation**: Utilizes a custom update server, API key authentication, configurable server URL, HTTPS with certificate validation, and non-blocking chunked HTTP reads with yield() calls to prevent UI freezing during version checks.
    *   **Timing**: Hardware interrupt timers (hw_timer_t for ESP32, Timer1 for ESP8266) for LED indicators ensure guaranteed execution even during blocking operations. Software tickers handle NeoPixel updates and OTA scheduling.

2.  **Dashboard Server**: A Python Flask application for firmware distribution and device management.
    *   **Features**: API-based firmware distribution supporting multiple platforms (ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, Wemos D1 Mini), upload/download endpoints with API key authentication, platform aliasing (e.g., ESP8266 to ESP-12F), SHA256 checksums, and status monitoring.
    *   **Deployment**: VM deployment (always-on) ensures consistent state across requests. Fully automatic CI/CD via GitHub Actions builds firmware for various board variants, generates timestamp-based versions (`year.month.day.buildnum`), and uploads binaries and configuration to the dashboard.
    *   **System Design Choices**: Emphasizes automated versioning, secure communication, and graceful fallback for optional services like Object Storage. Secrets are managed securely in GitHub and Replit, with GitHub Actions injecting configuration into the dashboard. VM deployment prevents multi-instance state synchronization issues.

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
-   **Adafruit_NeoPixel**: For controlling NeoPixel LED matrices.
-   **AutoConnect**: Arduino library for advanced WiFi management and captive portal.
-   **ESP32 Arduino Core 2.0.14**: Specific version required for AutoConnect compatibility.
-   **ESP8266 WiFi libraries**: Built-in for ESP8266 devices.
-   **Replit Object Storage** (Optional): For persistent storage of firmware binaries and version metadata, with graceful fallback to local filesystem.
-   **GitHub Actions**: CI/CD platform for automated firmware builds, versioning, and deployment to the dashboard.
## Recent Changes

- **2025-11-19**: Added AC_COEXIST to keep captive portal AP running alongside WiFi connection
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
