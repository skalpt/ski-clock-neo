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
    *   **Features**: Custom 5x7 pixel font, multi-panel support, freeze-proof LED status indicator with hardware interrupt timers, advanced WiFi management via `AutoConnect` (multi-network credentials, captive portal, background reconnection), secure non-blocking OTA updates via a custom server with API key authentication and HTTPS, and real-time MQTT heartbeat monitoring. NeoPixel updates use FreeRTOS tasks on ESP32 for smooth rendering.
    *   **MQTT Monitoring**: Publishes device heartbeats to HiveMQ Cloud broker (device ID, board type, firmware version, uptime, WiFi RSSI, free heap, SSID, IP). Subscribes to version update notifications. Uses TLS encryption without certificate validation. WiFi event-driven lifecycle management with automatic connect/disconnect and broker reconnection.

2.  **Dashboard Server**: A Python Flask application for firmware distribution and device management.
    *   **Features**: API-based firmware distribution supporting multiple platforms (ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, Wemos D1 Mini), upload/download endpoints with API key authentication, platform aliasing, SHA256 checksums, real-time device monitoring via MQTT, PostgreSQL database for persistent device tracking, and an interactive web dashboard.
    *   **MQTT Integration**: Background MQTT subscriber connects to HiveMQ Cloud, subscribes to heartbeat messages, saves devices to PostgreSQL on first heartbeat, updates `last_seen` timestamp, and exposes live device data via REST API. Web UI auto-refreshes to display connected devices with online/offline status, uptime, WiFi signal strength, network info, and firmware versions.
    *   **Device Management**: PostgreSQL database stores device registry. Dashboard shows online/offline status, device statistics, and allows device deletion.
    *   **Production Sync**: Development environment automatically syncs firmware metadata from production every 5 minutes via a background thread, updating the local database with new firmware versions.
    *   **Deployment**: VM deployment ensures consistent state and persistent MQTT connections. CI/CD via GitHub Actions automates firmware builds, versioning, credential injection, and binary uploads.
    *   **System Design Choices**: Emphasizes automated versioning, secure communication, and graceful fallback for optional services like Object Storage. Secrets are managed securely in GitHub and Replit.

## External Dependencies
-   **Firmware Libraries**:
    -   **Adafruit_NeoPixel**: For controlling NeoPixel LED matrices.
    -   **AutoConnect**: Arduino library for advanced WiFi management.
    -   **PubSubClient**: MQTT client library for ESP32/ESP8266 with TLS support.
    -   **ESP32 Arduino Core 2.0.14**: Specific version required for AutoConnect compatibility.
    -   **ESP8266 WiFi libraries**: Built-in for ESP8266 devices.
-   **Dashboard Dependencies**:
    -   **Flask**: Web framework for REST API and UI serving.
    -   **paho-mqtt**: Python MQTT client.
    -   **PostgreSQL**: Database for persistent storage.
    -   **Replit Object Storage** (Optional): For persistent storage of firmware binaries (.bin files), with graceful fallback to local filesystem.
-   **Cloud Services**:
    -   **HiveMQ Cloud Serverless**: MQTT broker for real-time device monitoring.
    -   **GitHub Actions**: CI/CD platform for automated firmware builds and deployment.