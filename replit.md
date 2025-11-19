# Ski Clock Neo - Project Documentation

## Overview
This project combines Arduino firmware for NeoPixel LED matrix displays with a custom firmware update server. The firmware runs on ESP32/ESP8266 hardware, while the dashboard server is hosted on Replit (easily migrated to self-hosted infrastructure).

**Architecture**: 
- **Firmware** (embedded C++ for ESP32/ESP8266) - Displays content on 16x16 NeoPixel matrix
- **Dashboard Server** (Python Flask) - Distributes firmware updates and monitors devices

## Project Structure

```
ski-clock-neo/
├── firmware/                    # Arduino firmware code
│   └── ski-clock-neo/           # Arduino sketch folder (required by Arduino CLI)
│       ├── ski-clock-neo.ino    # Main sketch
│       ├── font_5x7.h           # Font definitions
│       ├── neopixel_render.h    # LED rendering functions
│       ├── wifi_config.h        # WiFi management (AutoConnect)
│       ├── ota_update.h         # OTA update system
│       ├── certificates.h       # HTTPS root CA certificates
│       └── info.sh              # Information script
├── dashboard/              # Flask update server
│   ├── app.py              # Main Flask application
│   ├── requirements.txt    # Python dependencies
│   ├── firmwares/          # Uploaded firmware binaries (gitignored)
│   ├── config.json         # Configuration from GitHub Actions (gitignored)
│   └── .gitignore          # Dashboard-specific gitignore
├── .github/
│   └── workflows/
│       └── build-firmware.yml  # CI/CD for firmware builds
├── README.md               # User-facing documentation
└── replit.md               # This file - internal project notes
```

## Hardware Requirements
- **Supported boards**:
  - ESP32 (original), ESP32-C3, ESP32-S3
  - ESP-12F, ESP-01, Wemos D1 Mini (ESP8266)
- WS2812 NeoPixel LED matrix (16x16)
- Data pin connected to pin 4

## Firmware Features
- Custom 5x7 pixel font with 2x diagonal smoothing
- Multi-panel support with serpentine wiring
- **Advanced WiFi** (AutoConnect library):
  - Multiple network credentials with auto-fallback
  - Always-available captive portal
  - Background auto-reconnection
- **Secure OTA Updates**:
  - Custom update server (not GitHub-dependent)
  - API key authentication
  - Configurable server URL for easy migration
  - Dual-timer retry logic (1h success, 5m failure)
  - HTTPS support with cert validation

## Dashboard Server Features
- **API-based firmware distribution**
- **Multi-platform version management**: ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, Wemos D1 Mini, ESP8266 (legacy)
- **Upload endpoint** with canonical platform validation (rejects aliased uploads)
- **Download endpoint** with API key authentication and platform aliasing
- **Backward compatibility** for legacy ESP8266 devices (auto-mapped to ESP-12F firmware)
- **SHA256 checksums** for integrity verification
- **Status monitoring** showing all platforms including legacy alias

## Secrets Configuration

**Architecture Decision**: Secrets are stored ONLY in GitHub, not in Replit. The dashboard receives configuration automatically from GitHub Actions on each build.

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

## Recent Changes
- **2025-11-19**: **PRODUCTION-READY**: Multi-platform OTA firmware system with 6 board variants
- **2025-11-19**: Migrated firmware storage to Replit Object Storage for persistence across deployments
- **2025-11-19**: Added graceful fallback to local filesystem when Object Storage not configured
- **2025-11-19**: Created object_storage.py module for Google Cloud Storage integration
- **2025-11-19**: Fixed GitHub Actions to pin ESP32 core to v2.0.14 (AutoConnect compatibility)
- **2025-11-19**: Fixed ESP-01 FQBN to use generic board with 1M flash (esp8266:esp8266:generic:eesz=1M)
- **2025-11-19**: Added secrets to both GitHub and Replit for proper API authentication
- **2025-11-19**: Added upload validation - rejects aliased platform uploads (esp8266), requires canonical names
- **2025-11-19**: Fixed GitHub Actions config upload to use --fail (build fails on config rejection)
- **2025-11-19**: Made esp8266 a first-class platform with firmware mapping to esp12f for backward compatibility
- **2025-11-19**: Added multi-platform support (6 boards: ESP32/C3/S3, ESP12F, ESP-01, D1 Mini) with compile-time board detection
- **2025-11-19**: Added backward compatibility for legacy ESP8266 devices (automatically mapped to ESP12F firmware)
- **2025-11-19**: Added error checking to GitHub Actions workflow - build fails immediately if any upload fails
- **2025-11-19**: Fixed ESP8266 flash size configuration (use FQBN eesz=4M3M instead of build property)
- **2025-11-19**: Fixed ESP8266 Update API compatibility (getErrorString vs errorString)
- **2025-11-19**: Fixed FIRMWARE_VERSION redefinition warning (now uses #ifndef guard)
- **2025-11-19**: Fixed wifi_config.h to use AutoConnectCredential.entries() instead of portal.credential()
- **2025-11-19**: Pinned ESP32 Core to v2.0.14 for AutoConnect compatibility (v3.x not supported)
- **2025-11-19**: Restructured firmware to Arduino-standard folder layout (firmware/ski-clock-neo/ski-clock-neo.ino)
- **2025-11-19**: Fixed deployment configuration to use gunicorn for production
- **2025-11-19**: Implemented automatic timestamp-based versioning (year.month.day.buildnum)
- **2025-11-19**: Migrated to GitHub-only secrets (dashboard reads from config uploaded by Actions)
- **2025-11-19**: Changed workflow to trigger on push to main (no manual tagging required)
- **2025-11-19**: Updated version parsing in firmware and dashboard to support timestamp format
- **2025-11-19**: Added /api/config endpoint for GitHub Actions to upload configuration
- **2025-11-19**: Implemented custom firmware update server with Flask dashboard
- **2025-11-19**: Restructured project into firmware/ and dashboard/ directories  
- **2025-11-19**: Migrated from GitHub Releases to custom server for OTA updates
- **2025-11-19**: Added API key authentication for upload and download endpoints
- **2025-11-19**: Made UPDATE_SERVER_URL configurable (injected at build time)
- **2025-11-19**: Updated GitHub Actions to upload binaries to dashboard server
- **2025-11-19**: Migrated GITHUB_REPO_* from hardcoded to secrets-based config
- **2025-11-19**: Added OTA firmware update system with GitHub Releases integration
- **2025-11-19**: Replaced custom WiFi with AutoConnect library
- **2025-11-19**: Refactored code into modular headers
- **2025-11-19**: Initial import from GitHub

## Development Notes

### Testing Dashboard Locally
```bash
cd dashboard
python app.py
```
Access at `http://localhost:5000`

### Manual Firmware Upload
```bash
curl -X POST "$UPDATE_SERVER_URL/api/upload" \
  -H "X-API-Key: $UPLOAD_API_KEY" \
  -F "file=@firmware-esp32-v1.0.0.bin" \
  -F "version=v1.0.0" \
  -F "platform=esp32"
```

### Check Server Status
```bash
curl "$UPDATE_SERVER_URL/api/status"
```

### Workflow Configuration
- **Dashboard Server**: Runs on port 5000 (required for Replit webview)
- **Output type**: `webview` (accessible via Replit preview)
- **Command**: `cd dashboard && python app.py`

## Future Enhancements
- Device monitoring dashboard (track deployed devices)
- Update rollback capabilities
- Staged rollouts (percentage-based deployment)
- Device health metrics and telemetry
- Web UI for firmware management

## User Preferences
- Target platform: ESP32/ESP8266 (not standard Arduino)
- Multi-panel support is intentional (not a bug)
- Private repository (no public releases)
- Configurable server URL for future migration
