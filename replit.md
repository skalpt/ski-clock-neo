# Ski Clock Neo - Project Documentation

## Overview
Ski Clock Neo integrates Arduino firmware for NeoPixel LED matrix displays with a custom, secure firmware update server. The firmware is designed for ESP32/ESP8266 hardware, displaying dynamic content on a 16x16 NeoPixel matrix. The dashboard server, built with Python Flask, manages and distributes firmware updates, supporting various ESP board types. The project emphasizes automatic, secure, and robust over-the-air (OTA) updates, enabling seamless deployment and management of embedded devices. The system is designed for high reliability and ease of migration to self-hosted infrastructure.

## User Preferences
- Target platform: ESP32/ESP8266 (not standard Arduino)
- Multi-panel support is intentional (not a bug)
- Private repository (no public releases)
- Configurable server URL for future migration

## System Architecture
The project comprises two main components:
1.  **Firmware**: Embedded C++ for ESP32/ESP8266 microcontrollers, responsible for driving the 16x16 NeoPixel LED matrix.
    *   **Features**: Custom 5x7 pixel font with diagonal smoothing, multi-panel support (serpentine wiring), LED status indicator with board-agnostic `LED_BUILTIN` and direct port manipulation for performance, advanced WiFi management via `AutoConnect` library (multi-network credentials, captive portal, background reconnection), and secure OTA updates.
    *   **OTA Implementation**: Custom update server, API key authentication, configurable server URL, HTTPS support with certificate validation, and ticker-based scheduling for update checks.
    *   **Timing**: Uses software tickers for LED indicators, NeoPixel updates, and OTA checks, ensuring non-blocking operation.
2.  **Dashboard Server**: Python Flask application, primarily for firmware distribution and device management.
    *   **Features**: API-based firmware distribution, multi-platform version management (ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, Wemos D1 Mini), upload and download endpoints with API key authentication, platform aliasing (e.g., legacy ESP8266 maps to ESP-12F firmware), SHA256 checksums for integrity, and status monitoring.
    *   **Deployment**: Fully automatic CI/CD via GitHub Actions, which builds firmware for multiple board variants, generates timestamp-based versions (`year.month.day.buildnum`), and uploads binaries and configuration to the dashboard server.
    *   **UI/UX Decisions**: Not explicitly defined beyond API functionality; however, the Flask dashboard serves as the backend for management. The LED matrix provides the primary user-facing display.
    *   **System Design Choices**: Emphasis on automated versioning, secure communication, and graceful fallback for optional services like Object Storage. Secrets are managed in both GitHub and Replit, with GitHub Actions handling configuration injection into the dashboard.

## External Dependencies
-   **Adafruit_NeoPixel**: Library for controlling NeoPixel LED matrices.
-   **AutoConnect**: Arduino library for advanced WiFi management and captive portal functionality.
-   **ESP32 Arduino Core 2.0.14**: Specific version required for AutoConnect compatibility.
-   **ESP8266 WiFi libraries**: Built-in libraries for ESP8266 devices.
-   **Replit Object Storage** (Optional): Used for persistent storage of firmware binaries and version metadata, providing graceful fallback to local filesystem if not configured.
-   **GitHub Actions**: CI/CD platform for automated firmware builds, versioning, and deployment to the dashboard server.