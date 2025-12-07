# Norrtek IoT - Project Documentation

## Overview
Norrtek IoT is a multi-product Internet of Things (IoT) platform designed for managing various display devices, such as ski-clocks, bed-clocks, and signage. It integrates ESP32/ESP8266 firmware with NeoPixel LED matrix displays and a custom, secure firmware update server. The platform enables dynamic content display on 16x16 NeoPixel matrices and facilitates automatic, secure over-the-air (OTA) updates via a Python Flask dashboard. The project prioritizes robust deployment, simplified embedded device management, and easy migration to self-hosted infrastructure, aiming for high reliability and seamless updates across diverse IoT products.

## User Preferences
- Target platform: ESP32/ESP8266 (not standard Arduino)
- Multi-panel support is intentional (not a bug)
- Private repository (no public releases)
- Configurable server URL for future migration
- Multi-product architecture for managing different IoT device types

## System Architecture
The Norrtek IoT platform comprises two main components: a Firmware component for embedded devices and a Dashboard Server for management.

**UI/UX Decisions (Dashboard):**
The dashboard features a modern UI with CSS variables for light/dark mode, a contemporary color palette, improved typography, and responsive card-based layouts. It is branded as "Norrtek IoT - Device Fleet Management."

**Technical Implementations & Feature Specifications:**

**Firmware (Embedded C++ for ESP32/ESP8266):**
*   **Display Management:** Drives 16x16 NeoPixel matrices with a custom 5x7 pixel font, supports multi-panel setups, and includes a freeze-proof LED status indicator. NeoPixel updates utilize FreeRTOS tasks on ESP32 for smooth rendering. A modular display architecture separates hardware configuration (`display_config.h`), hardware-agnostic buffer management (`display_core`), and NeoPixel-specific rendering (`neopixel_render`). A 2x Glyph Override System provides hand-crafted 2x versions of problem glyphs for artistic control.
*   **Connectivity & Updates:** Manages WiFi via `AutoConnect`. Secure non-blocking OTA updates are handled via a custom server with API key authentication and HTTPS. OTA progress is reported via MQTT.
*   **MQTT Integration:** Uses environment-scoped topic architecture: `norrtek-iot/{env}/{path}/{device_id}` where `{env}` is either `dev` or `prod`. Topics include `heartbeat` for dynamic telemetry (rssi, uptime, free_heap, ssid, ip) published every 60s, and `info` for static device info (product, board, version, environment, config, supported_commands) published on connect, after config changes, and on-demand via 'info' command. Supports TLS encryption. Subscribes to device-specific topics for remote `rollback`, `restart`, `snapshot`, `info`, and `config` commands.
*   **Environment Scope:** Devices have an environment field (`dev` or `prod`) that determines their MQTT topic namespace. Environment is stored as a single-byte enum in EEPROM/NVS (0=default/dev, 1=dev, 2=prod) for efficiency. Changes trigger automatic MQTT reconnection to the new topic namespace. **Pending Environment Provisioning:** The `PENDING_ENV_SCOPE` compile-time flag allows devices to be provisioned directly to a specific environment on first boot. Dev builds (from `build-firmware.yml`) default to dev environment; prod builds (from `promote-prod.yml`) use `-DPENDING_ENV_SCOPE=2` to provision directly to prod. This value is written to persistent storage on first boot only; subsequent boots read from storage, and MQTT can still override the environment at runtime.
*   **Remote Configuration:** Devices subscribe to `norrtek-iot/{env}/config/{device_id}` for runtime configuration updates. Temperature offset (-20°C to +20°C) and environment are configurable via MQTT and persisted to NVS (ESP32) or EEPROM (ESP8266). Configuration changes emit events (config_updated, config_error, config_noop) for dashboard visibility.
*   **Display Content & Control:** Alternates time and date, displays temperature (DS18B20), and uses event-driven updates. A button-controlled timer mode provides stopwatch functionality with a state machine for transitions.
*   **Timekeeping:** Integrates DS3231 RTC for instant time on boot, with NTP syncing the RTC hourly.
*   **System Stability:** Uses FreeRTOS tasks/TickTwo library for deterministic display control and rendering. Critical sections ensure thread-safe access for event-driven rendering. A centralized LED Connectivity State Management system tracks WiFi and MQTT status.
*   **Event Logging:** A ring buffer stores and publishes device events (system, connectivity, temperature, RTC/Time, user input, display) to MQTT.
*   **Code Organization:** Consistent structure in `.cpp` files with section headers. Files are organized in `src/` for Arduino recursive compilation with specific include path conventions.

**Dashboard Server (Python Flask Application):**
*   **Firmware Management:** Provides a multi-product API for firmware distribution, supporting uploads with API key authentication, platform aliasing, and SHA256 checksums.
*   **Device Management:** Integrates with PostgreSQL for persistent device tracking, displaying status, environment scope, and allowing deletion. Monitors device heartbeat timestamps for "online," "degraded," and "offline" statuses. Devices include `environment` field indicating whether they are in `dev` or `prod` namespace. Devices can be renamed with friendly display names (e.g., "Hedvalla Ski Clock") via the edit button on device cards.
*   **MQTT Integration:** A background subscriber subscribes to BOTH `dev` and `prod` environment topics to manage all devices from a single dashboard. Processes heartbeats, persists data, and exposes live data via a REST API. Automatically checks firmware versions and sends update notifications via MQTT using the device's environment-specific topic.
*   **Data Visualization:** Stores and visualizes display snapshots in PostgreSQL with color-accurate canvas rendering. Provides a unified `/history` page with 4 tabs: Snapshots, Updates (OTA + USB Flash), Events, and Commands, featuring shared filters. Displays a live feed of the last 10 events.
*   **Command & Control:** Dedicated Commands tab in history page for sending remote commands (`temp_offset`, `rollback`, `restart`, `snapshot`) to devices via MQTT. Command history is logged in PostgreSQL with status tracking. Device cards link directly to the Commands tab filtered by device.
*   **Security:** Session-based authentication for dashboard routes and API key authentication for device API routes, with Role-Based Access Control (RBAC).
*   **Dev/Production Environment Separation:** Production acts as stable proxy for development. Dev environment auto-registers with production on startup using `PRODUCTION_API_URL` and maintains connection via 60-second heartbeats. Production provides `/api/dev/register`, `/api/dev/heartbeat`, `/api/dev/status`, `/api/dev/upload`, and `/api/dev/config` endpoints for proxy functionality. Dashboard header displays DEV/PROD badge based on `is_production()` detection. GitHub Actions: `build-firmware.yml` (auto-triggers on push to main, saves version manifest for promotion) and `promote-prod.yml` (manual trigger, fetches version from last successful dev build). Device environment scope is managed at runtime via MQTT, not at compile time. Legacy `deploy-prod.yml` is deprecated. Note: `deploy-dev.yml` should be deleted from GitHub as it's a duplicate.
*   **Deployment:** Supports VM deployment and GitHub Actions for CI/CD. Development environments sync firmware metadata from production.
*   **Browser-Based Flashing:** Integrates ESP Web Tools for direct USB flashing.

**System Design Choices:**
The architecture emphasizes automated versioning, secure communication, graceful fallback mechanisms, and a multi-product design. The database and API endpoints are product-aware, allowing for distinct firmware and device management across different IoT products. Firmware caching and device heartbeats also support product differentiation.

## External Dependencies
*   **Firmware Libraries:** Adafruit_NeoPixel OR FastLED, RTClib (Adafruit), AutoConnect, PubSubClient (with TLS support), ESP32 Arduino Core, ESP8266 WiFi libraries.
*   **Dashboard Dependencies:** Flask, paho-mqtt, PostgreSQL.
*   **Cloud Services:** HiveMQ Cloud Serverless (MQTT broker), GitHub Actions (CI/CD).