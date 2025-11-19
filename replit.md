# Ski Clock Neo - Project Documentation

## Overview
This project combines Arduino firmware for NeoPixel LED matrix displays with a custom firmware update server. The firmware runs on ESP32/ESP8266 hardware, while the dashboard server is hosted on Replit (easily migrated to self-hosted infrastructure).

**Architecture**: 
- **Firmware** (embedded C++ for ESP32/ESP8266) - Displays content on 16x16 NeoPixel matrix
- **Dashboard Server** (Python Flask) - Distributes firmware updates and monitors devices

## Project Structure

```
ski-clock-neo/
├── firmware/               # Arduino firmware code
│   ├── ski-clock-neo.ino   # Main sketch
│   ├── font_5x7.h          # Font definitions
│   ├── neopixel_render.h   # LED rendering functions
│   ├── wifi_config.h       # WiFi management (AutoConnect)
│   ├── ota_update.h        # OTA update system
│   ├── certificates.h      # HTTPS root CA certificates
│   └── info.sh             # Information script
├── dashboard/              # Flask update server
│   ├── app.py              # Main Flask application
│   ├── requirements.txt    # Python dependencies
│   ├── firmwares/          # Uploaded firmware binaries (gitignored)
│   └── .gitignore          # Dashboard-specific gitignore
├── .github/
│   └── workflows/
│       └── build-firmware.yml  # CI/CD for firmware builds
├── README.md               # User-facing documentation
└── replit.md               # This file - internal project notes
```

## Hardware Requirements
- **ESP32 or ESP8266** board with WiFi
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
- **Version management** per platform (ESP32/ESP8266)
- **Upload endpoint** for GitHub Actions integration
- **Download endpoint** with API key authentication
- **SHA256 checksums** for integrity verification
- **Status monitoring** for stored firmwares

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
   - Uploads config to dashboard server
   - Compiles firmware for ESP32 and ESP8266
   - Injects UPDATE_SERVER_URL and DOWNLOAD_API_KEY at build time
   - Uploads binaries to dashboard server with version metadata
   - Saves artifacts for manual download
3. **Dashboard** → Stores config and firmware, serves to devices
4. **Devices** → Check hourly, download, and install updates automatically

## Libraries Required
- **Adafruit_NeoPixel** - LED matrix control
- **AutoConnect** - WiFi management (install via Library Manager)
- ESP32/ESP8266 WiFi libraries (built-in)

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
