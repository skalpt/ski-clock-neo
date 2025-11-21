# Ski Clock Neo - Project Documentation

## Overview
Ski Clock Neo integrates Arduino firmware for ESP32/ESP8266 microcontrollers with NeoPixel LED matrix displays and a custom, secure firmware update server. The system provides dynamic content display on 16x16 NeoPixel matrices and enables automatic, secure over-the-air (OTA) updates via a Python Flask dashboard. This project aims for robust deployment, simplified management of embedded devices, and easy migration to self-hosted infrastructure, focusing on high reliability and seamless updates.

## User Preferences
- Target platform: ESP32/ESP8266 (not standard Arduino)
- Multi-panel support is intentional (not a bug)
- Private repository (no public releases)
- Configurable server URL for future migration

## System Architecture
The project comprises two main components:

**1. Firmware (Embedded C++ for ESP32/ESP8266):**
*   **Features**: Drives 16x16 NeoPixel matrices with a custom 5x7 pixel font, supports multi-panel setups, includes a freeze-proof LED status indicator using hardware interrupt timers, and manages WiFi via `AutoConnect` (multi-network, captive portal, background reconnection). Secure non-blocking OTA updates are handled via a custom server with API key authentication and HTTPS. NeoPixel updates utilize FreeRTOS tasks on ESP32 for smooth rendering.
*   **MQTT Integration**: Publishes device heartbeats (ID, board type, firmware version, uptime, WiFi RSSI, free heap, SSID, IP) to HiveMQ Cloud. Manages version checking by requesting updates on boot and hourly, receiving device-specific responses, and triggering OTA. Subscribes to broadcast `skiclock/version/updates` for real-time notifications. Supports TLS encryption without certificate validation.
*   **MQTT Command Handling**: Subscribes to device-specific topics for remote management, supporting `rollback` (via dual OTA partition switching on ESP32, re-download on ESP8266) and `restart` commands.
*   **OTA Progress Reporting**: Publishes real-time OTA status (start, progress, complete) to MQTT topics, allowing granular tracking and dashboard updates.

**2. Dashboard Server (Python Flask application):**
*   **Features**: Provides API-based firmware distribution for multiple platforms (ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, Wemos D1 Mini), supports firmware upload with API key authentication, platform aliasing, and SHA256 checksums. Integrates with PostgreSQL for persistent device tracking and offers an interactive web dashboard.
*   **MQTT Integration**: Background subscriber connects to HiveMQ Cloud, processes heartbeats and version requests, persists device data to PostgreSQL, and exposes live device data via REST API. Responds to version requests with the latest firmware info.
*   **Degraded Status Detection**: Monitors device heartbeat timestamps to identify "online," "degraded" (2+ missed checkins), and "offline" statuses.
*   **OTA Update Logging & Monitoring**: Tracks all OTA attempts in PostgreSQL, logging progress and status via MQTT. The dashboard displays real-time statistics, provides an OTA history page with filtering, and shows live progress bars.
*   **MQTT Command System**: Enables sending remote `rollback` and `restart` commands to devices via MQTT, with API endpoints and dashboard buttons for control.
*   **Device Management**: Stores device registry in PostgreSQL, showing status and allowing deletion.
*   **Authentication & Authorization**: Uses session-based authentication for dashboard routes and API key authentication for device API routes. Features Role-Based Access Control (RBAC) with platform-level permissions and user-scoped firmware downloads via signed URL tokens for security.
*   **Browser-Based Firmware Flashing**: Integrates ESP Web Tools for direct USB flashing from the browser (Chrome/Edge), supporting quick and full flash modes (bootloader recovery) using manifest files.
*   **Production Sync**: Development environments automatically sync firmware metadata from production every 5 minutes.
*   **Deployment**: Relies on VM deployment for consistent state and GitHub Actions for CI/CD, automating firmware builds and uploads.
*   **System Design**: Emphasizes automated versioning, secure communication, and graceful fallback for storage.

## External Dependencies
*   **Firmware Libraries**: Adafruit_NeoPixel, AutoConnect, PubSubClient (with TLS support), ESP32 Arduino Core 2.0.14, ESP8266 WiFi libraries.
*   **Dashboard Dependencies**: Flask, paho-mqtt, PostgreSQL, Replit Object Storage (optional, with local filesystem fallback).
*   **Cloud Services**: HiveMQ Cloud Serverless (MQTT broker), GitHub Actions (CI/CD).