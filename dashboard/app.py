from flask import Flask, request, jsonify, send_file, Response, render_template
import os
import hashlib
import json
import io
from datetime import datetime
from pathlib import Path
from typing import Dict, Optional, Any
from object_storage import ObjectStorageService, ObjectNotFoundError, ObjectStorageError
from mqtt_subscriber import start_mqtt_subscriber, get_device_status

app = Flask(__name__)

# Start MQTT subscriber in background thread
mqtt_client = start_mqtt_subscriber()

# Object Storage paths
VERSIONS_OBJECT_PATH = "versions.json"
FIRMWARES_PREFIX = "firmwares/"

# Fallback local paths (used during development if Object Storage not available)
FIRMWARES_DIR = Path(__file__).parent / 'firmwares'
FIRMWARES_DIR.mkdir(exist_ok=True)
CONFIG_FILE = Path(__file__).parent / 'config.json'
VERSIONS_FILE = Path(__file__).parent / 'versions.json'

# Load configuration from file (uploaded by GitHub Actions) or fallback to environment
def load_config():
    if CONFIG_FILE.exists():
        try:
            with open(CONFIG_FILE, 'r') as f:
                config = json.load(f)
                return config
        except Exception as e:
            print(f"Warning: Could not load config file: {e}")
    
    # Fallback to environment variables
    return {
        'UPLOAD_API_KEY': os.getenv('UPLOAD_API_KEY', 'dev-key-change-in-production'),
        'DOWNLOAD_API_KEY': os.getenv('DOWNLOAD_API_KEY', 'dev-download-key'),
        'UPDATE_SERVER_URL': os.getenv('UPDATE_SERVER_URL', '')
    }

# Load initial config
config = load_config()
UPLOAD_API_KEY = config.get('UPLOAD_API_KEY')
DOWNLOAD_API_KEY = config.get('DOWNLOAD_API_KEY')

# Supported platforms (must match firmware board detection and GitHub Actions)
SUPPORTED_PLATFORMS = [
    'esp32',      # ESP32 (original)
    'esp32c3',    # ESP32-C3
    'esp32s3',    # ESP32-S3
    'esp12f',     # ESP-12F (ESP8266)
    'esp01',      # ESP-01 (ESP8266)
    'd1mini',     # Wemos D1 Mini (ESP8266)
    'esp8266'     # Legacy ESP8266 (backward compatibility - aliases to esp12f)
]

# Platform aliases: Map legacy platform names to specific board firmware
# Used to serve correct firmware when legacy devices request generic platform
PLATFORM_FIRMWARE_MAPPING = {
    'esp8266': 'esp12f',  # Legacy ESP8266 devices get ESP-12F firmware
}

# Check if Object Storage is available
def is_object_storage_available():
    try:
        storage = ObjectStorageService()
        storage.get_bucket_name()
        return True
    except ObjectStorageError:
        return False

# Load firmware versions (try Object Storage first, fall back to local file)
def load_versions():
    # Try Object Storage first
    try:
        storage = ObjectStorageService()
        bucket_name = storage.get_bucket_name()
        
        if storage.exists(VERSIONS_OBJECT_PATH):
            versions_json = storage.download_as_string(VERSIONS_OBJECT_PATH)
            saved_versions = json.loads(versions_json)
            print(f"✓ Loaded {len([v for v in saved_versions.values() if v])} firmware versions from Object Storage")
            return saved_versions
    except (ObjectStorageError, ObjectNotFoundError) as e:
        print(f"Object Storage not available for versions, trying local fallback: {e}")
    
    # Fall back to local file system
    if VERSIONS_FILE.exists():
        try:
            with open(VERSIONS_FILE, 'r') as f:
                saved_versions = json.load(f)
                print(f"Loaded {len([v for v in saved_versions.values() if v])} firmware versions from local file")
                return saved_versions
        except Exception as e:
            print(f"Warning: Could not load local versions file: {e}")
    
    # Initialize with None for all platforms
    return {platform: None for platform in SUPPORTED_PLATFORMS}

# Save firmware versions (to Object Storage if available, otherwise local file)
def save_versions():
    try:
        storage = ObjectStorageService()
        bucket_name = storage.get_bucket_name()
        
        versions_json = json.dumps(LATEST_VERSIONS, indent=2)
        storage.upload_string(VERSIONS_OBJECT_PATH, versions_json)
        print(f"✓ Saved firmware versions to Object Storage")
    except ObjectStorageError as e:
        print(f"Object Storage not available, saving to local file: {e}")
        try:
            with open(VERSIONS_FILE, 'w') as f:
                json.dump(LATEST_VERSIONS, f, indent=2)
            print(f"Saved firmware versions to local file: {VERSIONS_FILE}")
        except Exception as e:
            print(f"Error saving versions file: {e}")

LATEST_VERSIONS: Dict[str, Optional[Dict[str, Any]]] = load_versions()

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api')
def api_index():
    return jsonify({
        'service': 'Ski Clock Firmware Server',
        'status': 'running',
        'supported_platforms': SUPPORTED_PLATFORMS,
        'endpoints': {
            'version': '/api/version?platform=<platform>',
            'download': '/api/firmware/<platform>',
            'upload': '/api/upload (POST)',
            'status': '/api/status'
        }
    })

@app.route('/api/version')
def get_version():
    platform = request.args.get('platform', '').lower()
    original_platform = platform  # Save for download_url consistency
    is_aliased = False
    
    if not platform:
        return jsonify({
            'error': 'Missing platform parameter',
            'supported_platforms': SUPPORTED_PLATFORMS
        }), 400
    
    # Apply platform firmware mapping for backward compatibility
    if platform in PLATFORM_FIRMWARE_MAPPING:
        actual_platform = PLATFORM_FIRMWARE_MAPPING[platform]
        print(f"Platform firmware mapping: {platform} → {actual_platform}")
        is_aliased = True
        platform = actual_platform
    
    if platform not in SUPPORTED_PLATFORMS:
        return jsonify({
            'error': f'Invalid platform: {platform}',
            'supported_platforms': SUPPORTED_PLATFORMS
        }), 400
    
    version_info = LATEST_VERSIONS.get(platform)
    
    if not version_info:
        return jsonify({'error': 'No firmware available for this platform'}), 404
    
    # Only modify download_url for aliased requests (backward compatibility)
    if is_aliased:
        response_info = version_info.copy()
        response_info['download_url'] = f'/api/firmware/{original_platform}'
        return jsonify(response_info)
    
    return jsonify(version_info)

@app.route('/api/firmware/<platform>')
def download_firmware(platform):
    api_key = request.headers.get('X-API-Key')
    
    if api_key != DOWNLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    platform = platform.lower()
    
    # Apply platform firmware mapping for backward compatibility
    if platform in PLATFORM_FIRMWARE_MAPPING:
        actual_platform = PLATFORM_FIRMWARE_MAPPING[platform]
        print(f"Platform firmware mapping: {platform} → {actual_platform}")
        platform = actual_platform
    
    if platform not in SUPPORTED_PLATFORMS:
        return jsonify({
            'error': f'Invalid platform: {platform}',
            'supported_platforms': SUPPORTED_PLATFORMS
        }), 400
    
    version_info = LATEST_VERSIONS.get(platform)
    
    if not version_info:
        return jsonify({'error': 'No firmware available'}), 404
    
    # Check where the firmware is stored
    storage_location = version_info.get('storage', 'local')
    
    if storage_location == 'object_storage':
        # Download from Object Storage using stored full path
        object_path = None  # Initialize to avoid UnboundLocalError
        try:
            storage = ObjectStorageService()
            
            # Use the full object_path if available (includes bucket)
            object_path = version_info.get('object_path')
            if not object_path:
                # Fallback: reconstruct from object_name and current bucket
                bucket_name = storage.get_bucket_name()
                object_path = f"/{bucket_name}/{version_info.get('object_name', FIRMWARES_PREFIX + version_info['filename'])}"
            
            # Download as bytes and stream to client
            firmware_data = storage.download_as_bytes(object_path)
            
            return Response(
                firmware_data,
                mimetype='application/octet-stream',
                headers={
                    'Content-Disposition': f'attachment; filename="{version_info["filename"]}"',
                    'Content-Length': str(len(firmware_data))
                }
            )
        except (ObjectStorageError, ObjectNotFoundError) as e:
            error_msg = f"Error downloading from Object Storage"
            if object_path:
                error_msg += f" ({object_path})"
            print(f"{error_msg}: {e}")
            # Try local filesystem as last resort
            print(f"Attempting local filesystem fallback...")
            firmware_path = FIRMWARES_DIR / version_info['filename']
            if firmware_path.exists():
                print(f"✓ Found firmware in local fallback")
                return send_file(
                    firmware_path,
                    mimetype='application/octet-stream',
                    as_attachment=True,
                    download_name=version_info['filename']
                )
            return jsonify({'error': 'Firmware file not found in storage or local fallback'}), 404
    else:
        # Download from local filesystem
        firmware_path = FIRMWARES_DIR / version_info.get('local_path', version_info['filename'])
        if not Path(firmware_path).is_absolute():
            firmware_path = FIRMWARES_DIR / firmware_path
        
        if not firmware_path.exists():
            return jsonify({'error': 'Firmware file not found'}), 404
        
        return send_file(
            firmware_path,
            mimetype='application/octet-stream',
            as_attachment=True,
            download_name=version_info['filename']
        )

def parse_version(version_str: str) -> int:
    """
    Parse version string to comparable integer.
    Supports both formats:
    - Semantic: v1.2.3 -> 1002003 (major*1000000 + minor*1000 + patch)
    - Timestamp: 2025.11.19.1 -> year*100000000 + month*1000000 + day*10000 + build
    """
    if version_str.startswith('v') or version_str.startswith('V'):
        version_str = version_str[1:]
    
    parts = version_str.split('.')
    
    if len(parts) != 4:
        # Semantic versioning (v1.2.3)
        major = int(parts[0]) if len(parts) > 0 else 0
        minor = int(parts[1]) if len(parts) > 1 else 0
        patch = int(parts[2]) if len(parts) > 2 else 0
        return major * 1000000 + minor * 1000 + patch
    else:
        # Timestamp versioning (2025.11.19.1)
        year = int(parts[0])
        month = int(parts[1])
        day = int(parts[2])
        build = int(parts[3])
        # Year starts at 2025, so normalize: (year-2025)*100000000
        return (year - 2025) * 100000000 + month * 1000000 + day * 10000 + build

@app.route('/api/upload', methods=['POST'])
def upload_firmware():
    api_key = request.headers.get('X-API-Key')
    
    if api_key != UPLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    if 'file' not in request.files:
        return jsonify({'error': 'No file provided'}), 400
    
    file = request.files['file']
    version = request.form.get('version')
    platform = request.form.get('platform', '').lower()
    
    if not version or not platform:
        return jsonify({'error': 'Missing version or platform'}), 400
    
    # Reject uploads for aliased platforms - only accept canonical board names
    if platform in PLATFORM_FIRMWARE_MAPPING:
        canonical_platform = PLATFORM_FIRMWARE_MAPPING[platform]
        return jsonify({
            'error': f'Cannot upload for aliased platform "{platform}"',
            'message': f'Please upload using canonical platform name: "{canonical_platform}"',
            'canonical_platform': canonical_platform
        }), 400
    
    if platform not in SUPPORTED_PLATFORMS:
        return jsonify({
            'error': f'Invalid platform: {platform}',
            'supported_platforms': SUPPORTED_PLATFORMS
        }), 400
    
    if not file.filename or not file.filename.endswith('.bin'):
        return jsonify({'error': 'File must be a .bin file'}), 400
    
    try:
        new_version_code = parse_version(version)
    except (ValueError, IndexError):
        return jsonify({'error': 'Invalid version format. Use vX.Y.Z'}), 400
    
    current_version_info = LATEST_VERSIONS.get(platform)
    if current_version_info:
        try:
            current_version_code = parse_version(current_version_info['version'])
            if new_version_code < current_version_code:
                return jsonify({
                    'error': 'Version downgrade not allowed',
                    'current_version': current_version_info['version'],
                    'attempted_version': version
                }), 400
            elif new_version_code == current_version_code:
                return jsonify({
                    'error': 'Version already exists',
                    'current_version': current_version_info['version'],
                    'message': 'Use a newer version number to update'
                }), 409
        except (ValueError, IndexError):
            pass
    
    filename = f"firmware-{platform}-{version}.bin"
    object_name = f"{FIRMWARES_PREFIX}{filename}"
    
    # Read file data
    file_data = file.read()
    file_size = len(file_data)
    file_hash = hashlib.sha256(file_data).hexdigest()
    
    # Initialize variables to avoid UnboundLocalError
    object_path = None
    filepath = None
    
    # Try to save to Object Storage, fall back to local filesystem
    try:
        storage = ObjectStorageService()
        bucket_name = storage.get_bucket_name()
        
        # Save to temp file then upload (Object Storage requires file path)
        temp_path = FIRMWARES_DIR / filename
        with open(temp_path, 'wb') as f:
            f.write(file_data)
        
        object_path = storage.upload_file(temp_path, object_name)
        print(f"✓ Uploaded firmware to Object Storage: {object_path}")
        
        # Clean up temp file after successful upload
        temp_path.unlink()
        
        storage_location = "object_storage"
    except ObjectStorageError as e:
        print(f"Object Storage not available, saving to local filesystem: {e}")
        # Fall back to local filesystem
        filepath = FIRMWARES_DIR / filename
        with open(filepath, 'wb') as f:
            f.write(file_data)
        storage_location = "local"
    
    version_info = {
        'version': version,
        'filename': filename,
        'size': file_size,
        'sha256': file_hash,
        'uploaded_at': datetime.utcnow().isoformat(),
        'download_url': f'/api/firmware/{platform}',
        'storage': storage_location
    }
    
    # Store full object path for Object Storage downloads (includes bucket)
    if storage_location == 'object_storage' and object_path:
        version_info['object_path'] = object_path
        version_info['object_name'] = object_name
    elif filepath:
        version_info['local_path'] = str(filepath)
    
    LATEST_VERSIONS[platform] = version_info
    
    # Mirror esp12f firmware to legacy esp8266 alias for monitoring visibility
    if platform == 'esp12f':
        LATEST_VERSIONS['esp8266'] = version_info.copy()
        LATEST_VERSIONS['esp8266']['download_url'] = '/api/firmware/esp8266'
        LATEST_VERSIONS['esp8266']['note'] = 'Legacy alias for ESP-12F (backward compatibility)'
    
    # Persist versions to disk (survives server restarts)
    save_versions()
    
    return jsonify({
        'success': True,
        'message': f'Firmware uploaded successfully',
        'version_info': version_info
    }), 201

@app.route('/api/config', methods=['POST'])
def update_config():
    """Receive config updates from GitHub Actions"""
    global UPLOAD_API_KEY, DOWNLOAD_API_KEY, config
    
    api_key = request.headers.get('X-API-Key')
    
    # Use current UPLOAD_API_KEY for authentication
    if api_key != UPLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    try:
        new_config = request.get_json()
        
        # Validate required fields
        required_fields = ['UPDATE_SERVER_URL', 'UPLOAD_API_KEY', 'DOWNLOAD_API_KEY']
        for field in required_fields:
            if field not in new_config:
                return jsonify({'error': f'Missing required field: {field}'}), 400
        
        # Save to config file
        with open(CONFIG_FILE, 'w') as f:
            json.dump(new_config, f, indent=2)
        
        # Reload config
        config = load_config()
        UPLOAD_API_KEY = config.get('UPLOAD_API_KEY')
        DOWNLOAD_API_KEY = config.get('DOWNLOAD_API_KEY')
        
        return jsonify({
            'success': True,
            'message': 'Configuration updated successfully',
            'config_file': str(CONFIG_FILE)
        }), 200
    except Exception as e:
        return jsonify({'error': f'Failed to update config: {str(e)}'}), 500

@app.route('/api/status')
def status():
    return jsonify({
        'firmwares': LATEST_VERSIONS,
        'storage': {
            'files': [f.name for f in FIRMWARES_DIR.glob('*.bin')],
            'count': len(list(FIRMWARES_DIR.glob('*.bin')))
        },
        'config_source': 'file' if CONFIG_FILE.exists() else 'environment'
    })

@app.route('/api/devices')
def devices():
    device_list = get_device_status()
    return jsonify({
        'devices': device_list,
        'count': len(device_list),
        'mqtt_enabled': mqtt_client is not None
    })

if __name__ == '__main__':
    # Development mode - debug enabled
    # Production deployments use gunicorn instead
    import os
    debug_mode = os.getenv('FLASK_ENV') == 'development'
    app.run(host='0.0.0.0', port=5000, debug=debug_mode)
