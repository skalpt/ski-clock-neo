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

*   **Library Architecture:** The firmware is organized as a reusable PlatformIO library at `firmware/lib/norrtek-iot/` with three layers:
    - **Core Layer** (`core/`): Platform-agnostic utilities - `debug.h`, `device_info`, `event_log`, `led_indicator`, `timer_helpers`. Core modules cannot depend on connectivity or display.
    - **Connectivity Layer** (`connectivity/`): Network modules - `mqtt_client`, `ota_update`, `wifi_config`. These depend on core but not display.
    - **Display Layer** (`display/`): Hardware-agnostic buffer management - `display_core`, `text_renderer`, `font_5x7`. Separated from hardware-specific NeoPixel rendering.
    - **Layering Rule**: Core modules use callbacks for cross-layer communication (e.g., `event_log` uses `setEventPublishCallback()` instead of importing MQTT directly).

*   **Unified API:** Products include single header `norrtek_iot.h` and call:
    - `initNorrtekIoT(ProductConfig)` - Initializes all subsystems with product-specific configuration
    - `processNorrtekIoT()` - Main loop handler for WiFi, MQTT, and timers
    - `ProductConfig` struct contains: product name, display dimensions, panel layout, GPIO pins, colors, brightness

*   **Product-Aware MQTT Topics:** Topic structure `norrtek-iot/<product>/<subtopic>/<device_id>` with helper functions:
    - `buildDeviceTopic(subtopic)` - Returns `norrtek-iot/ski-clock-neo/heartbeat/abc123`
    - `buildProductTopic(subtopic)` - Returns `norrtek-iot/ski-clock-neo/command`

*   **Display Management:** Drives 16x16 NeoPixel matrices with a custom 5x7 pixel font, supports multi-panel setups, and includes a freeze-proof LED status indicator. NeoPixel updates utilize FreeRTOS tasks on ESP32 for smooth rendering. Display initialization accepts `DisplayInitConfig` with rows, panel dimensions, and panels-per-row array. A 2x Glyph Override System provides hand-crafted 2x versions of problem glyphs for artistic control.

*   **Connectivity & Updates:** Manages WiFi via `AutoConnect`. Secure non-blocking OTA updates are handled via a custom server with API key authentication and HTTPS. OTA progress is reported via MQTT.

*   **MQTT Integration:** Publishes device heartbeats and display snapshots to HiveMQ Cloud. Supports TLS encryption. Subscribes to device-specific topics for remote `rollback`, `restart`, and `snapshot` commands.

*   **Display Content & Control:** Alternates time and date, displays temperature (DS18B20), and uses event-driven updates. A button-controlled timer mode provides stopwatch functionality with a state machine for transitions.

*   **Timekeeping:** Integrates DS3231 RTC for instant time on boot, with NTP syncing the RTC hourly.

*   **System Stability:** Uses FreeRTOS tasks/TickTwo library for deterministic display control and rendering. Critical sections ensure thread-safe access for event-driven rendering. A centralized LED Connectivity State Management system tracks WiFi and MQTT status.

*   **Event Logging:** A ring buffer stores and publishes device events (system, connectivity, temperature, RTC/Time, user input, display) to MQTT via registered callbacks.

*   **Code Organization:** Consistent structure in `.cpp` files with section headers. Library modules in `firmware/lib/norrtek-iot/`, product-specific code in `firmware/ski-clock-neo/src/`.

*   **Ported Products:** ski-clock-neo is now ported to use the unified library API. Product-specific modules (data_time, data_temperature, data_button, display_controller, fastled_render) use `<norrtek_iot.h>` for library includes. Duplicated core/connectivity modules have been removed.

**Dashboard Server (Python Flask Application):**
*   **Firmware Management:** Provides a multi-product API for firmware distribution, supporting uploads with API key authentication, platform aliasing, and SHA256 checksums.
*   **Device Management:** Integrates with PostgreSQL for persistent device tracking, displaying status, and allowing deletion. Monitors device heartbeat timestamps for "online," "degraded," and "offline" statuses.
*   **MQTT Integration:** A background subscriber processes heartbeats, persists data, and exposes live data via a REST API. Automatically checks firmware versions and sends update notifications via MQTT.
*   **Data Visualization:** Stores and visualizes display snapshots in PostgreSQL with color-accurate canvas rendering. Provides a unified `/history` page with tabs for Snapshots, OTA Updates, and Events, featuring shared filters. Displays a live feed of the last 10 events.
*   **Command & Control:** Enables sending remote `rollback`, `restart`, and `snapshot` commands via API endpoints and dashboard buttons.
*   **Security:** Session-based authentication for dashboard routes and API key authentication for device API routes, with Role-Based Access Control (RBAC).
*   **Deployment:** Supports VM deployment and GitHub Actions for CI/CD. Development environments sync firmware metadata from production.
*   **Browser-Based Flashing:** Integrates ESP Web Tools for direct USB flashing.

**System Design Choices:**
The architecture emphasizes automated versioning, secure communication, graceful fallback mechanisms, and a multi-product design. The database and API endpoints are product-aware, allowing for distinct firmware and device management across different IoT products. Firmware caching and device heartbeats also support product differentiation.

## External Dependencies
*   **Firmware Libraries:** Adafruit_NeoPixel OR FastLED, RTClib (Adafruit), AutoConnect, PubSubClient (with TLS support), ESP32 Arduino Core, ESP8266 WiFi libraries.
*   **Dashboard Dependencies:** Flask, paho-mqtt, PostgreSQL.
*   **Cloud Services:** HiveMQ Cloud Serverless (MQTT broker), GitHub Actions (CI/CD).