# Norrtek IoT - Dashboard Server

Flask-based firmware update server and device monitoring dashboard for ESP32/ESP8266 devices with MQTT telemetry, PostgreSQL persistence, and browser-based flashing. Supports multiple products with independent firmware versioning.

## Features

### Firmware Management
- **Multi-Platform Support**: ESP32, ESP32-C3, ESP32-S3, ESP-12F, ESP-01, D1 Mini
- **Version History**: Full version history with rollback capability
- **Secure Uploads**: API key authentication for CI/CD pipelines
- **SHA256 Verification**: Integrity checking for all firmware files
- **Platform Aliases**: Legacy ESP8266 devices get ESP-12F firmware automatically
- **Full Flash Support**: Bootloader and partition table for factory resets

### Device Monitoring
- **MQTT Telemetry**: Real-time heartbeats, events, and OTA progress
- **Device Registry**: PostgreSQL-backed device tracking
- **Status Detection**: Online/degraded/offline with configurable thresholds
- **Display Snapshots**: Color-accurate visualization of device displays
- **Event Logging**: Comprehensive event history with filtering

### Dashboard UI
- **Modern Design**: CSS variables with automatic light/dark mode
- **Responsive Layout**: Card-based design for all screen sizes
- **Unified History**: Tabbed interface for snapshots, OTA updates, and events
- **Live Updates**: Auto-refresh for device status and events
- **ESP Web Tools**: Browser-based USB flashing

### Security
- **Session Authentication**: Secure login for dashboard access
- **Role-Based Access Control**: Per-platform download permissions
- **API Key Authentication**: Separate keys for upload and download
- **Signed Download URLs**: Time-limited tokens for firmware downloads
- **Production Validation**: Fail-closed configuration enforcement

## Supported Platforms

| Platform | Chip Family | Board Examples |
|----------|-------------|----------------|
| esp32 | ESP32 | ESP32 DevKit, ESP32-WROOM |
| esp32c3 | ESP32-C3 | ESP32-C3 SuperMini, XIAO |
| esp32s3 | ESP32-S3 | ESP32-S3 DevKit |
| esp12f | ESP8266 | ESP-12F modules |
| esp01 | ESP8266 | ESP-01, ESP-01S |
| d1mini | ESP8266 | Wemos D1 Mini |

## Project Structure

```
dashboard/
├── app.py              # Main Flask application
├── models.py           # SQLAlchemy database models
├── mqtt_subscriber.py  # MQTT background subscriber
├── object_storage.py   # Object storage abstraction
├── requirements.txt    # Python dependencies
├── firmwares/          # Local firmware storage
├── static/
│   └── app.css         # Dashboard styles
└── templates/
    ├── base.html       # Base template with navigation
    ├── index.html      # Device dashboard
    ├── history.html    # Unified history (snapshots/OTA/events)
    └── login.html      # Authentication page
```

## Database Models

| Model | Purpose |
|-------|---------|
| User | Dashboard authentication |
| Role | Permission groups |
| PlatformPermission | Per-platform download access |
| Device | Device registry and status |
| FirmwareVersion | Firmware metadata and history |
| OTAUpdateLog | OTA update tracking |
| HeartbeatHistory | Device connectivity history |
| DisplaySnapshot | Display capture history |
| EventLog | Device event history |
| DownloadLog | Firmware download audit |

## Environment Variables

### Required
| Variable | Description |
|----------|-------------|
| `DATABASE_URL` | PostgreSQL connection string |
| `UPLOAD_API_KEY` | API key for firmware uploads (CI/CD) |
| `DOWNLOAD_API_KEY` | API key for device downloads |
| `MQTT_HOST` | MQTT broker hostname |
| `MQTT_USERNAME` | MQTT authentication username |
| `MQTT_PASSWORD` | MQTT authentication password |

### Optional
| Variable | Description | Default |
|----------|-------------|---------|
| `FLASK_SECRET_KEY` | Session encryption key | Random (insecure) |
| `PRODUCTION_API_URL` | Production server for dev sync | None |
| `DEV_MODE` | Force development mode | false |
| `DASHBOARD_USERNAME` | Legacy auth (deprecated) | None |
| `DASHBOARD_PASSWORD` | Legacy auth (deprecated) | None |

## API Endpoints

### Firmware API (Device-Facing)

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/api/version?platform=<platform>` | None | Get latest version info |
| GET | `/api/firmware/<platform>` | API Key | Download firmware binary |
| POST | `/api/upload` | API Key | Upload new firmware |
| GET | `/api/status` | None | Server and firmware status |

### Device API

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/api/devices` | Session | List all devices |
| GET | `/api/devices/<id>` | Session | Get device details |
| DELETE | `/api/devices/<id>` | Session | Delete device |
| POST | `/api/devices/<id>/command` | Session | Send MQTT command |
| POST | `/api/devices/<id>/pin` | Session | Pin firmware version |

### History API

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/api/ota-updates` | Session | OTA update history |
| GET | `/api/snapshots` | Session | Display snapshot history |
| GET | `/api/events` | Session | Device event history |

### ESP Web Tools

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| GET | `/api/manifest/<platform>` | Token | ESP Web Tools manifest |
| GET | `/api/firmware-download` | Token | Signed firmware download |

## MQTT Topics

### Subscriptions (Inbound)
| Topic | Purpose |
|-------|---------|
| `skiclock/heartbeat/+` | Device heartbeats |
| `skiclock/snapshot/+` | Display snapshots |
| `skiclock/events/+` | Device events |
| `skiclock/ota/+` | OTA progress updates |

### Publications (Outbound)
| Topic | Purpose |
|-------|---------|
| `skiclock/command/<deviceId>` | Remote commands |
| `skiclock/update/<deviceId>` | Update notifications |

## Running Locally

### Prerequisites
- Python 3.11+
- PostgreSQL database
- MQTT broker (e.g., HiveMQ Cloud)

### Setup

```bash
# Install dependencies
pip install -r requirements.txt

# Set environment variables
export DATABASE_URL="postgresql://user:pass@host:5432/dbname"
export UPLOAD_API_KEY="your-upload-key"
export DOWNLOAD_API_KEY="your-download-key"
export MQTT_HOST="broker.hivemq.com"
export MQTT_USERNAME="your-mqtt-user"
export MQTT_PASSWORD="your-mqtt-pass"

# Run server
python app.py
```

Server runs on `http://0.0.0.0:5000`

### Create Admin User

```python
# In Python shell with app context
from app import app, db
from models import User, Role

with app.app_context():
    admin_role = Role.query.filter_by(name='admin').first()
    user = User(email='admin@example.com', role_id=admin_role.id)
    user.set_password('your-secure-password')
    db.session.add(user)
    db.session.commit()
```

## Deployment

### Replit
- Port: 5000 (required for webview)
- Secrets: Managed via Replit Secrets
- Database: Replit PostgreSQL integration
- Workflow: Auto-starts on Repl boot

### Production Checklist
1. Set strong `FLASK_SECRET_KEY`
2. Configure production API keys
3. Enable HTTPS (handled by Replit/reverse proxy)
4. Create admin user in database
5. Configure MQTT credentials

## GitHub Actions Integration (Optional)

You can set up GitHub Actions workflows to automate firmware builds:
1. Generate timestamp-based versions (`YYYY.MM.DD.BUILD`)
2. Compile firmware for all platforms
3. Inject server URL and API keys
4. Upload binaries to this server

### Upload Endpoint

Use this endpoint for manual uploads or from CI/CD pipelines:

```bash
curl -X POST "https://your-server/api/upload" \
  -H "X-API-Key: $UPLOAD_API_KEY" \
  -F "file=@firmware-esp32-v2025.1.15.1.bin" \
  -F "version=2025.1.15.1" \
  -F "platform=esp32"
```

## Testing

### Check Server Status
```bash
curl "http://localhost:5000/api/status"
```

### Check Version
```bash
curl "http://localhost:5000/api/version?platform=esp32"
```

### Download Firmware (Device)
```bash
curl -H "X-API-Key: $DOWNLOAD_API_KEY" \
  "http://localhost:5000/api/firmware/esp32" \
  -o firmware.bin
```

## Display Snapshot Format

### New Per-Row Format (Variable Panels)
```json
{
  "rows": [
    {
      "text": "12:34",
      "cols": 3,
      "width": 48,
      "height": 16,
      "mono": "base64...",
      "monoColor": [255, 0, 0, 10]
    },
    {
      "text": "68°F",
      "cols": 4,
      "width": 64,
      "height": 16,
      "mono": "base64...",
      "monoColor": [255, 0, 0, 10]
    }
  ]
}
```

### Legacy Format (Uniform Grid)
```json
{
  "rows": 2,
  "cols": 3,
  "width": 48,
  "height": 32,
  "pixels": "base64..."
}
```

The dashboard automatically detects and renders both formats.

## Production Sync (Development Only)

In development environments, the server syncs firmware metadata from production every 5 minutes. This allows testing with real firmware versions without uploading separately.

Configure with:
```bash
export PRODUCTION_API_URL="https://production-server.example.com"
```

## License

See repository root for license information.
