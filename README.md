# Ski Clock Neo

Arduino project for a NeoPixel LED matrix clock display with custom firmware update server.

## Project Structure

This project is organized into two main components:

### 1. **Firmware** (`firmware/` directory)
Arduino sketch designed to run on ESP32/ESP8266 hardware with NeoPixel LED matrices.

### 2. **Dashboard Server** (`dashboard/` directory)
Flask web application that hosts firmware updates and will eventually monitor deployed devices.

## Hardware Setup

- **Microcontroller**: ESP32 or ESP8266 (for WiFi support)
- **LED Matrix**: 16x16 WS2812 NeoPixels
- **Data Pin**: Pin 4

## Firmware Features

- **Custom 5x7 pixel font** with smooth 2x scaling
- **Advanced WiFi Management** (AutoConnect library):
  - Multiple network credential storage
  - Auto-fallback between saved networks
  - Portal always available for network switching
  - Background auto-reconnect on dropouts
- **Secure OTA Firmware Updates**:
  - Custom update server (Replit-hosted, easily migrated)
  - API key authentication for security
  - Automatic version checking
  - Periodic update checks (1 hour interval, 5 minute retry on failure)
  - HTTPS support with certificate validation

## Firmware Dependencies

- **[Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)** - LED matrix control
- **[AutoConnect](https://github.com/Hieromon/AutoConnect)** - WiFi management library
  - Install via Arduino IDE: Library Manager → Search "AutoConnect"
  - Or PlatformIO: `hieromon/AutoConnect@^1.4.2`
- ESP32/ESP8266 WiFi libraries (included with board package)

## Firmware Code Structure

The firmware is cleanly organized into modular components:
- **ski-clock-neo.ino**: Main sketch with constants, setup(), and loop()
- **font_5x7.h**: Font definitions for all displayable characters (0-9, :, -, ., °, C)
- **neopixel_render.h**: All NeoPixel rendering functions
- **wifi_config.h**: WiFi management and captive portal
- **ota_update.h**: OTA firmware update system with custom server integration
- **certificates.h**: Root CA certificates for HTTPS validation

## Dashboard Server

The dashboard server handles firmware distribution and will monitor deployed devices.

### Features
- **Firmware Upload API**: Receives binaries from GitHub Actions
- **Firmware Download API**: Serves updates to ESP32/ESP8266 devices
- **Version Management**: Tracks latest version per platform
- **API Key Authentication**: Separate keys for upload and download
- **SHA256 Checksums**: Integrity verification for firmware files

### API Endpoints

- `GET /` - Server status and endpoint list
- `GET /api/version?platform=esp32|esp8266` - Get latest version info
- `GET /api/firmware/<platform>` - Download firmware (requires API key)
- `POST /api/upload` - Upload new firmware (requires upload API key)
- `GET /api/status` - View all stored firmwares

## Setup Instructions

### 1. Configure Secrets (GitHub Only)

All secrets are managed in **GitHub only**. The dashboard receives them automatically from GitHub Actions.

#### GitHub Repository Secrets (Required)
1. Go to GitHub repo → Settings → Secrets and variables → Actions
2. Add repository secrets:
   - `UPDATE_SERVER_URL` - Your Replit deployment URL (e.g., `https://ski-clock-neo.yourname.repl.co`)
   - `UPLOAD_API_KEY` - Key for GitHub Actions to upload firmware (generate a random 32+ character string)
   - `DOWNLOAD_API_KEY` - Key for devices to download firmware (generate a different random 32+ character string)

**Note**: You do NOT need to configure secrets in Replit. The dashboard automatically receives configuration from GitHub Actions on each build.

### 2. Deploy the Dashboard

The dashboard runs automatically on Replit at port 5000. 

1. Click the **"Publish"** button in Replit to get a permanent URL
2. Copy your published URL (e.g., `https://ski-clock-neo.yourname.repl.co`)
3. Set this URL as `UPDATE_SERVER_URL` in GitHub secrets

### 3. Automatic Firmware Builds

**No manual tagging required!** Every push to the `main` branch automatically:

1. Generates a version number: `year.month.day.buildnum` (e.g., `2025.11.19.1`)
2. Uploads configuration to your dashboard server
3. Compiles firmware for ESP32 and ESP8266
4. Injects UPDATE_SERVER_URL and DOWNLOAD_API_KEY
5. Uploads binaries to your dashboard server
6. Saves artifacts for manual download

Simply push your changes:

```bash
git add .
git commit -m "Your changes"
git push origin main
```

GitHub Actions handles the rest automatically!

### 4. WiFi Setup on Device

On first boot, the device creates a WiFi access point:
- **SSID**: `SkiClock-Setup`
- **Password**: `configure`

Connect to this network and the configuration portal opens automatically. Select your WiFi network, enter the password, and save.

### 5. Automatic Updates

Once connected to WiFi:
- Device checks for updates every hour
- Downloads and installs automatically when newer version available
- Retries every 5 minutes on failure
- Reboots after successful update

## WiFi Management

### Advanced Features
- **Multiple Networks**: Add multiple WiFi networks - device tries them in order
- **Always Accessible**: Portal remains available even when connected
  - Connect to `SkiClock-Setup` anytime to manage networks
- **Auto-Reconnect**: Handles temporary WiFi dropouts gracefully
- **Network Roaming**: Automatically switches to strongest available network

### Network Management
To add or switch networks:
1. Connect to `SkiClock-Setup` WiFi network
2. Portal opens automatically (or go to 192.168.4.1)
3. Add new networks or remove old ones
4. Device saves all changes automatically

## Development Workflow

### Local Development
1. Edit firmware code in `firmware/` directory
2. Test dashboard server locally (runs on port 5000)
3. Commit and push changes

### Creating Releases
1. Tag with version number: `git tag v1.0.1`
2. Push tag: `git push origin v1.0.1`
3. GitHub Actions builds and uploads to dashboard
4. Deployed devices auto-update within 1 hour

### Manual Firmware Upload

You can also upload firmware manually using curl:

```bash
curl -X POST "https://your-server.repl.co/api/upload" \
  -H "X-API-Key: YOUR_UPLOAD_API_KEY" \
  -F "file=@firmware-esp32-v1.0.0.bin" \
  -F "version=v1.0.0" \
  -F "platform=esp32"
```

## Migrating to Self-Hosted Server

The UPDATE_SERVER_URL is configurable, making it easy to migrate:

1. Set up your own server running the dashboard Flask app
2. Update `UPDATE_SERVER_URL` secret in both Replit and GitHub
3. Create a new release to rebuild firmware with new URL
4. Devices will update to new firmware and start using your server

## Security

- **Private Repository**: Keep your repo private to protect firmware
- **API Keys**: Separate keys for upload (CI/CD) and download (devices)
- **HTTPS Support**: Dashboard supports HTTPS for secure transmission
- **No Secrets in Code**: All credentials injected at build time

## Running This Code

**Note**: The firmware code cannot run in the Replit browser environment as it requires physical Arduino hardware.

To use the firmware:
1. Use GitHub Actions to build (recommended)
2. Or compile locally with Arduino IDE/PlatformIO
3. Upload to your ESP32/ESP8266 board
4. Connect NeoPixel matrix to pin 4

## License

See repository for license information.
