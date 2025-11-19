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
    *   **Features**: Custom 5x7 pixel font with diagonal smoothing, multi-panel support, board-agnostic LED status indicator with fast port manipulation, advanced WiFi management via `AutoConnect` (multi-network credentials, captive portal, background reconnection), and secure OTA updates.
    *   **OTA Implementation**: Utilizes a custom update server, API key authentication, configurable server URL, HTTPS with certificate validation, and ticker-based scheduling for non-blocking update checks.
    *   **Timing**: Employs software tickers for LED indicators, NeoPixel updates, and OTA checks to ensure non-blocking operation.

2.  **Dashboard Server**: A Python Flask application for firmware distribution and device management.
    *   **Features**: API-based firmware distribution supporting multiple platforms (ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, Wemos D1 Mini), upload/download endpoints with API key authentication, platform aliasing (e.g., ESP8266 to ESP-12F), SHA256 checksums, and status monitoring.
    *   **Deployment**: Fully automatic CI/CD via GitHub Actions builds firmware for various board variants, generates timestamp-based versions (`year.month.day.buildnum`), and uploads binaries and configuration to the dashboard.
    *   **System Design Choices**: Emphasizes automated versioning, secure communication, and graceful fallback for optional services like Object Storage. Secrets are managed securely in GitHub and Replit, with GitHub Actions injecting configuration into the dashboard.

## Build Optimizations

### ESP32-C3 Size Optimization
The ESP32-C3 has limited flash space and requires special build optimizations:
- **Link Time Optimization (LTO)**: Enabled with `-flto` flag for aggressive code size reduction
- **Size Optimization**: Uses `-Os` compiler flag to prioritize code size over speed
- **C++ RTTI/Exceptions Disabled**: Uses `-fno-rtti -fno-exceptions` to remove unused C++ features
- **Dead Code Elimination**: Uses `-Wl,--gc-sections` linker flag to remove unused functions

### Debug Logging System
To save ~10-20KB of flash space, all `Serial.print` statements are gated behind a `DEBUG_LOGGING` flag:
- **Default**: Debug logging is **disabled** in production builds (saves flash space)
- **Enable**: Uncomment `#define DEBUG_LOGGING` in `firmware/ski-clock-neo/debug.h` to enable Serial output
- **Macros**: All code uses `DEBUG_PRINT()` and `DEBUG_PRINTLN()` instead of `Serial.print()`
- **Benefits**: When disabled, all debug strings are removed from the compiled binary

## External Dependencies
-   **Adafruit_NeoPixel**: For controlling NeoPixel LED matrices.
-   **AutoConnect**: Arduino library for advanced WiFi management and captive portal.
-   **ESP32 Arduino Core 2.0.14**: Specific version required for AutoConnect compatibility.
-   **ESP8266 WiFi libraries**: Built-in for ESP8266 devices.
-   **Replit Object Storage** (Optional): For persistent storage of firmware binaries and version metadata, with graceful fallback to local filesystem.
-   **GitHub Actions**: CI/CD platform for automated firmware builds, versioning, and deployment to the dashboard.