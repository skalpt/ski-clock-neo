# Ski Clock Neo - Project Documentation

## Overview
Ski Clock Neo integrates Arduino firmware for NeoPixel LED matrix displays with a custom, secure firmware update server. The firmware is designed for ESP32/ESP8266 hardware, displaying dynamic content on a 16x16 NeoPixel matrix. The dashboard server, built with Python Flask, manages and distributes firmware updates, supporting various ESP board types. The project emphasizes automatic, secure, and robust over-the-air (OTA) updates, enabling seamless deployment and management of embedded devices. The system is designed for high reliability and ease of migration to self-hosted infrastructure.

## User Preferences
- Target platform: ESP32/ESP8266 (not standard Arduino)
- Multi-panel support is intentional (not a bug)
- Private repository (no public releases)
- Configurable server URL for future migration

## Project Structure

```
ski-clock-neo/
├── firmware/                    # Arduino firmware code
│   └── ski-clock-neo/           # Arduino sketch folder (required by Arduino CLI)
│       ├── ski-clock-neo.ino    # Main sketch
│       ├── led_indicator.h      # LED status indicator with fast GPIO
│       ├── font_5x7.h           # Font definitions
│       ├── neopixel_render.h    # LED rendering functions
│       ├── wifi_config.h        # WiFi management (AutoConnect)
│       ├── ota_update.h         # OTA update system
│       └── certificates.h       # HTTPS root CA certificates
├── dashboard/                   # Flask update server
│   ├── app.py                   # Main Flask application
│   ├── object_storage.py        # Object Storage integration
│   ├── requirements.txt         # Python dependencies
│   ├── firmwares/               # Uploaded firmware binaries (gitignored)
│   ├── config.json              # Configuration from GitHub Actions (gitignored)
│   └── .gitignore               # Dashboard-specific gitignore
├── .github/
│   └── workflows/
│       └── build-firmware.yml   # CI/CD for firmware builds
├── README.md                    # User-facing documentation
└── replit.md                    # This file - internal project notes
```

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

## Hardware Requirements
- **Supported boards**:
  - ESP32 (original), ESP32-C3, ESP32-S3
  - ESP-12F, ESP-01, Wemos D1 Mini (ESP8266)
- WS2812 NeoPixel LED matrix (16x16)
- Data pin connected to pin 4

## Firmware Features

- Custom 5x7 pixel font with 2x diagonal smoothing
- Multi-panel support with serpentine wiring
- **LED Status Indicator** (built-in LED):
  - Uses LED_BUILTIN constant with fallback to GPIO2 for boards without native definition
  - Handles ESP32/ESP8266 inverted pins automatically (HIGH=off, LOW=on)
  - Quick flash during setup (interrupt-driven ticker)
  - 1 flash + 2s pause when WiFi connected
  - 3 flashes + 2s pause when WiFi disconnected
  - **Fast port manipulation** for performance in interrupt context:
    - ESP8266: GPOS/GPOC registers
    - ESP32 original: GPIO.out_w1ts/w1tc registers
    - ESP32-C3/S3: GPIO.out_w1ts.val/w1tc.val with dual GPIO bank support (pins 0-31 and 32-48)
  - Serial output shows GPIO pin number during setup for verification
- **Advanced WiFi** (AutoConnect library):
  - Multiple network credentials with auto-fallback
  - Always-available captive portal (non-blocking)
  - Background auto-reconnection
  - Responsive immediately at boot (no blocking delays)
- **Secure OTA Updates**:
  - Custom update server (not GitHub-dependent)
  - API key authentication
  - Configurable server URL for easy migration
  - Software ticker-based scheduling (initial check at 30s, then hourly self-scheduling)
  - Dual-timer retry logic (1h success, 5m failure)
  - HTTPS support with cert validation
- **Ticker-Based Timing**:
  - LED indicator: Interrupt-driven ticker with fast port manipulation
  - NeoPixel updates: Software ticker calls updateNeoPixels() directly (2s interval)
  - OTA checks: Software ticker with self-scheduling (30s initial, 1h recurring)
  - No blocking delays in setup() or loop()

## Dashboard Server Features
- **API-based firmware distribution**
- **Multi-platform version management**: ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, Wemos D1 Mini, ESP8266 (legacy)
- **Upload endpoint** with canonical platform validation (rejects aliased uploads)
- **Download endpoint** with API key authentication and platform aliasing
- **Backward compatibility** for legacy ESP8266 devices (auto-mapped to ESP-12F firmware)
- **SHA256 checksums** for integrity verification
- **Status monitoring** showing all platforms including legacy alias
- **Persistent storage**: Object Storage integration with graceful local fallback

## Object Storage Configuration (Optional)

**Purpose**: Firmware binaries and versions.json persist across Replit deployments using Object Storage (Google Cloud Storage).

**Without Object Storage**:
- ✅ System works normally using local filesystem
- ⚠️ Firmware files lost when Replit republishes the deployment
- ℹ️ GitHub Actions re-uploads firmware on every build (no data loss)

**With Object Storage** (recommended for production):
1. Create Object Storage bucket in Replit (Tools → Object Storage)
2. Set `OBJECT_STORAGE_BUCKET` environment variable to your bucket name
3. Restart the Dashboard Server workflow
4. Firmware persists across deployments automatically

**What gets stored**:
- ✅ Firmware binaries (`firmwares/*.bin`) → Object Storage
- ✅ Version metadata (`versions.json`) → Object Storage
- ❌ API keys/secrets (`config.json`) → Replit Secrets (NOT Object Storage)

**Architecture Decision**: Graceful fallback ensures the system works with or without Object Storage.

## Secrets Configuration

**Architecture Decision**: Secrets are stored in BOTH GitHub (for Actions) and Replit (for dashboard authentication).

### GitHub Repository Secrets (⚠️ Required)
Must be configured in GitHub repo (Settings → Secrets → Actions):
- `UPDATE_SERVER_URL` - Dashboard server URL (your Replit deployment)
- `UPLOAD_API_KEY` - Used by GitHub Actions to upload binaries and config
- `DOWNLOAD_API_KEY` - Embedded in firmware for downloads

### Dashboard Configuration
- Dashboard reads from `config.json` (uploaded by GitHub Actions)
- Fallback to environment variables for backward compatibility
- `/api/config` endpoint receives config updates from GitHub Actions
- `config.json` is gitignored to prevent accidental secret commits

## Versioning System

**Automatic Timestamp-Based Versioning**: No manual tagging required!

- **Format**: `year.month.day.buildnum` (e.g., `2025.11.19.1`)
- **Generation**: Automatic on every push to `main` branch
- **Build number**: GitHub Actions `run_number` (auto-increments)
- **Comparison**: `(year-2025)*100000000 + month*1000000 + day*10000 + build`
- **Legacy support**: Still supports semantic versioning (`v1.2.3`) for backward compatibility

## Deployment Flow

**Fully Automatic** - just push to main:

1. **Code Changes** → `git push origin main`
2. **GitHub Actions** (automatic):
   - Generates timestamp version (e.g., `2025.11.19.1`)
   - Creates config.json with all three secrets
   - Uploads config to dashboard server (with error checking)
   - Compiles firmware for 6 board variants (ESP32/C3/S3, ESP-12F, ESP-01, D1 Mini)
   - Injects UPDATE_SERVER_URL, DOWNLOAD_API_KEY, and BOARD_* defines at build time
   - Uploads binaries to dashboard server with canonical platform names
   - Mirrors ESP-12F firmware to esp8266 entry for legacy compatibility
   - Saves artifacts for manual download
   - **Build fails immediately** if config or firmware upload fails
3. **Dashboard** → Stores config and firmware, serves to devices with platform aliasing
4. **Devices** → Check hourly, download board-specific (or aliased) firmware, install updates automatically

## Libraries Required
- **Adafruit_NeoPixel** - LED matrix control
- **AutoConnect** - WiFi management (install via Library Manager)
- **ESP32 Arduino Core 2.0.14** - Required for AutoConnect compatibility (Core 3.x not supported)
- ESP8266 WiFi libraries (built-in)

## WiFi Configuration
- **First boot**: Device creates `SkiClock-Setup` AP (password: `configure`)
- **Captive portal**: Opens automatically for network selection
- **Multi-network**: Stores multiple credentials, tries in order
- **Always accessible**: Portal remains available even when connected

## OTA Update System

### Version Checking
- Devices check every 1 hour when successful
- Retry every 5 minutes on failure
- **Timestamp format**: `2025.11.19.1` → `(year-2025)*100000000 + month*1000000 + day*10000 + build`
- **Legacy support**: `v1.2.3` → `1002003` (major*1000000 + minor*1000 + patch)
- Only updates when newer version available

### Security
- **Private repo**: Firmware binaries never exposed publicly
- **API key auth**: Devices authenticate with DOWNLOAD_API_KEY
- **HTTPS optional**: Supports both HTTP and HTTPS
- **Configurable endpoint**: Easy migration to self-hosted server

### Architecture Decision: Custom Server vs GitHub
**Chose custom server** instead of GitHub Releases because:
- ✅ Repository stays private (no public releases needed)
- ✅ No GitHub PAT tokens in firmware (security)
- ✅ Full control over distribution and monitoring
- ✅ Easy migration to self-hosted infrastructure
- ✅ Enables future device monitoring features

## External Dependencies
-   **Adafruit_NeoPixel**: Library for controlling NeoPixel LED matrices.
-   **AutoConnect**: Arduino library for advanced WiFi management and captive portal functionality.
-   **ESP32 Arduino Core 2.0.14**: Specific version required for AutoConnect compatibility.
-   **ESP8266 WiFi libraries**: Built-in libraries for ESP8266 devices.
-   **Replit Object Storage** (Optional): Used for persistent storage of firmware binaries and version metadata, providing graceful fallback to local filesystem if not configured.
-   **GitHub Actions**: CI/CD platform for automated firmware builds, versioning, and deployment to the dashboard server.

## Recent Changes

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
