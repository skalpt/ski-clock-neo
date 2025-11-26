# Ski Clock Neo - Project Documentation

## Overview
Ski Clock Neo integrates ESP32/ESP8266 firmware with NeoPixel LED matrix displays and a custom, secure firmware update server. This system enables dynamic content display on 16x16 NeoPixel matrices and facilitates automatic, secure over-the-air (OTA) updates via a Python Flask dashboard. The project emphasizes robust deployment, simplified embedded device management, and easy migration to self-hosted infrastructure, aiming for high reliability and seamless updates.

## User Preferences
- Target platform: ESP32/ESP8266 (not standard Arduino)
- Multi-panel support is intentional (not a bug)
- Private repository (no public releases)
- Configurable server URL for future migration

## System Architecture
The project consists of two primary components:

**1. Firmware (Embedded C++ for ESP32/ESP8266):**
*   **Features**: Drives 16x16 NeoPixel matrices with a custom 5x7 pixel font, supports multi-panel setups, includes a freeze-proof LED status indicator using hardware interrupt timers, and manages WiFi via `AutoConnect`. Secure non-blocking OTA updates are handled via a custom server with API key authentication and HTTPS. NeoPixel updates utilize FreeRTOS tasks on ESP32 for smooth rendering.
*   **Modular Display Architecture**: Hardware configuration is centralized in `display_config.h`. A generic `display.{h,cpp}` library manages a bit-packed display buffer and text content, which is hardware-agnostic. The `neopixel_render.h` renderer handles pixel transformations (rotation, serpentine wiring) and commits unified frames. This architecture supports future migration to HUB75 panels.
*   **FreeRTOS Display Task**: Display rendering runs in a dedicated FreeRTOS task (ESP32) or Ticker callback (ESP8266) for immediate, non-blocking display updates.
*   **Deterministic Display Controller Task**: Content scheduling (e.g., time/date toggling) runs in a dedicated FreeRTOS task (ESP32) or TickTwo library (ESP8266) to prevent display "freezing" during network operations.
*   **Centralized LED Connectivity State Management**: A priority-based system tracks WiFi and MQTT status, with a single API `setConnectivityState(wifi, mqtt)` to prevent race conditions. OTA updates use an override mode.
*   **Production-Ready Event-Driven Rendering**: Uses critical sections for thread-safe access to shared state, ensuring zero race conditions or visual glitches during concurrent updates.
*   **MQTT Integration**: Publishes device heartbeats to HiveMQ Cloud every 60 seconds. The dashboard automatically checks for new firmware versions and sends update notifications. Supports TLS encryption and increased buffer size for display snapshots. Topic structure uses consistent `skiclock/{type}/{deviceId}` pattern for device-specific messages.
*   **Display Snapshot System**: Publishes hourly snapshots of the display state to MQTT, including base64-encoded bit-packed pixel data and row text. On-demand snapshots are supported via MQTT commands.
*   **MQTT Command Handling**: Subscribes to device-specific topics for remote management, supporting `rollback`, `restart`, and `snapshot` commands.
*   **OTA Progress Reporting**: Publishes real-time OTA status to MQTT topics for granular tracking.
*   **Display Content System**: Alternates time and date every 4 seconds. Temperature (DS18B20 sensor) is displayed with non-blocking reads. Event-driven updates via `setText()` callbacks are used for all data libraries.
*   **RTC Integration**: DS3231 RTC module provides instant time on boot (before WiFi/NTP connects). NTP automatically syncs the RTC hourly to maintain accuracy. Falls back gracefully to NTP-only if no RTC is present.
*   **Time Change Callbacks**: Precise minute/date change detection via callback system. The data_time library tracks last minute and day, calling registered callbacks with flags (TIME_CHANGE_MINUTE, TIME_CHANGE_DATE) when changes occur. A 1-second polling timer detects changes reliably.
*   **Unified Timer Library**: The `timer_task` library provides platform abstraction for periodic timers - FreeRTOS tasks (priority 2, Core 1 on dual-core ESP32, Core 0 on C3) on ESP32 and TickTwo tickers on ESP8266. Supports both periodic timers (createTimer) and one-shot timers (createOneShotTimer/triggerTimer). Used by display_controller (4s toggle), data_time (1s time check), and data_temperature (30s poll + 750ms read delay).
*   **Clean Separation of Concerns**: Temperature polling and time/date toggling are owned by their respective libraries, which update the display controller via callbacks, ensuring independent timing logic.
*   **Event Logging System**: A ring buffer stores device events with timestamps, publishing them to MQTT when connected. Events include boot, WiFi connect/disconnect, MQTT connect/disconnect, and temperature readings.

**2. Dashboard Server (Python Flask application):**
*   **Features**: Provides an API for firmware distribution for multiple platforms, supporting uploads with API key authentication, platform aliasing, and SHA256 checksums. Integrates with PostgreSQL for persistent device tracking and offers an interactive web dashboard.
*   **Modern UI Design**: Redesigned dashboard with CSS variables for light/dark mode, modern color palette, improved typography, and responsive card-based layouts.
*   **MQTT Integration**: A background subscriber processes heartbeats, persists device data to PostgreSQL, and exposes live data via a REST API. Automatically checks firmware versions on each heartbeat and sends update notifications via MQTT.
*   **Unified History Page**: A single `/history` page with tabs for Snapshots, OTA Updates, and Events, featuring shared filter panels and URL state management.
*   **Display Snapshot Visualization**: Stores and visualizes display snapshots in PostgreSQL, offering color-accurate canvas rendering with a grid overlay.
*   **Live Events Feed**: Displays the last 10 events with auto-refresh and a link to the full history.
*   **Degraded Status Detection**: Monitors device heartbeat timestamps to determine "online," "degraded," and "offline" statuses.
*   **OTA Update Logging & Monitoring**: Tracks OTA attempts in PostgreSQL, logging progress and status via MQTT, and displaying real-time statistics and history.
*   **MQTT Command System**: Enables sending remote `rollback`, `restart`, and `snapshot` commands via API endpoints and dashboard buttons.
*   **Device Management**: Stores device registry in PostgreSQL, showing status and allowing deletion.
*   **Authentication & Authorization**: Session-based authentication for dashboard routes and API key authentication for device API routes, with Role-Based Access Control (RBAC).
*   **Browser-Based Firmware Flashing**: Integrates ESP Web Tools for direct USB flashing via manifest files.
*   **Production Sync**: Development environments sync firmware metadata from production every 5 minutes.
*   **Deployment**: Relies on VM deployment and GitHub Actions for CI/CD.
*   **System Design**: Emphasizes automated versioning, secure communication, and graceful fallback.

## External Dependencies
*   **Firmware Libraries**: Adafruit_NeoPixel, RTClib (Adafruit), AutoConnect, PubSubClient (with TLS support), ESP32 Arduino Core, ESP8266 WiFi libraries.
*   **Dashboard Dependencies**: Flask, paho-mqtt, PostgreSQL, Replit Object Storage (optional).
*   **Cloud Services**: HiveMQ Cloud Serverless (MQTT broker), GitHub Actions (CI/CD).