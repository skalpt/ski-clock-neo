# Ski Clock Neo - Dashboard Server

Flask-based firmware update server for ESP32/ESP8266 devices.

## Features

- **Firmware Upload**: Receive binaries from GitHub Actions CI/CD
- **Firmware Download**: Serve updates to ESP32/ESP8266 devices  
- **Version Management**: Track latest version per platform
- **API Authentication**: Separate keys for upload and download
- **SHA256 Checksums**: Integrity verification

## API Endpoints

### GET /
Server status and endpoint listing.

### GET /api/version?platform=esp32|esp8266
Get latest firmware version information for a platform.

**Response:**
```json
{
  "version": "v1.0.0",
  "filename": "firmware-esp32-v1.0.0.bin",
  "size": 847392,
  "sha256": "abc123...",
  "uploaded_at": "2025-11-19T10:52:30.123456",
  "download_url": "/api/firmware/esp32"
}
```

### GET /api/firmware/<platform>
Download firmware binary for a platform.

**Headers Required:**
- `X-API-Key: <DOWNLOAD_API_KEY>`

**Response:** Binary firmware file

### POST /api/upload
Upload new firmware version.

**Headers Required:**
- `X-API-Key: <UPLOAD_API_KEY>`

**Form Data:**
- `file`: Binary firmware file (.bin)
- `version`: Version string (e.g., "v1.0.0")
- `platform`: Platform name ("esp32" or "esp8266")

**Response:**
```json
{
  "success": true,
  "message": "Firmware uploaded successfully",
  "version_info": { ... }
}
```

### GET /api/status
View all stored firmwares and server status.

**Response:**
```json
{
  "firmwares": {
    "esp32": { ... },
    "esp8266": { ... }
  },
  "storage": {
    "files": ["firmware-esp32-v1.0.0.bin", ...],
    "count": 2
  }
}
```

## Environment Variables

- `UPLOAD_API_KEY` - Authentication key for firmware uploads (required)
- `DOWNLOAD_API_KEY` - Authentication key for firmware downloads (required)

## Running Locally

```bash
# Install dependencies
pip install -r requirements.txt

# Set environment variables
export UPLOAD_API_KEY="your-upload-key"
export DOWNLOAD_API_KEY="your-download-key"

# Run server
python app.py
```

Server runs on `http://0.0.0.0:5000`

## Deployment on Replit

The server is configured to run automatically on Replit:
- Port: 5000 (required for webview)
- Environment: Secrets are managed via Replit Secrets
- Workflow: Auto-starts on Repl boot

## Security

- **API Keys**: All endpoints require authentication
- **Separate Keys**: Upload and download use different keys
- **HTTPS**: Supports HTTPS when deployed (recommended)
- **File Validation**: Only .bin files accepted, platform validation

## Storage

Firmware binaries are stored in `firmwares/` directory:
- `firmware-esp32-v1.0.0.bin`
- `firmware-esp8266-v1.0.0.bin`
- etc.

This directory is gitignored to avoid committing large binaries.

## Testing

### Upload a firmware
```bash
curl -X POST "http://localhost:5000/api/upload" \
  -H "X-API-Key: your-upload-key" \
  -F "file=@firmware-esp32-v1.0.0.bin" \
  -F "version=v1.0.0" \
  -F "platform=esp32"
```

### Check version
```bash
curl "http://localhost:5000/api/version?platform=esp32"
```

### Download firmware
```bash
curl -H "X-API-Key: your-download-key" \
  "http://localhost:5000/api/firmware/esp32" \
  -o firmware.bin
```

### Check status
```bash
curl "http://localhost:5000/api/status"
```

## Future Enhancements

- Device monitoring and telemetry
- Web UI for firmware management
- Update rollback capabilities
- Staged rollouts (percentage-based)
- Device health metrics
- Update history and audit logs
