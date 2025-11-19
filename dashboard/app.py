from flask import Flask, request, jsonify, send_file
import os
import hashlib
import json
from datetime import datetime
from pathlib import Path
from typing import Dict, Optional, Any

app = Flask(__name__)

FIRMWARES_DIR = Path(__file__).parent / 'firmwares'
FIRMWARES_DIR.mkdir(exist_ok=True)

CONFIG_FILE = Path(__file__).parent / 'config.json'

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

LATEST_VERSIONS: Dict[str, Optional[Dict[str, Any]]] = {
    platform: None for platform in SUPPORTED_PLATFORMS
}

@app.route('/')
def index():
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
    
    firmware_path = FIRMWARES_DIR / version_info['filename']
    
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
    filepath = FIRMWARES_DIR / filename
    
    file.save(filepath)
    
    file_size = filepath.stat().st_size
    
    with open(filepath, 'rb') as f:
        file_hash = hashlib.sha256(f.read()).hexdigest()
    
    version_info = {
        'version': version,
        'filename': filename,
        'size': file_size,
        'sha256': file_hash,
        'uploaded_at': datetime.utcnow().isoformat(),
        'download_url': f'/api/firmware/{platform}'
    }
    
    LATEST_VERSIONS[platform] = version_info
    
    # Mirror esp12f firmware to legacy esp8266 alias for monitoring visibility
    if platform == 'esp12f':
        LATEST_VERSIONS['esp8266'] = version_info.copy()
        LATEST_VERSIONS['esp8266']['download_url'] = '/api/firmware/esp8266'
        LATEST_VERSIONS['esp8266']['note'] = 'Legacy alias for ESP-12F (backward compatibility)'
    
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

if __name__ == '__main__':
    # Development mode - debug enabled
    # Production deployments use gunicorn instead
    import os
    debug_mode = os.getenv('FLASK_ENV') == 'development'
    app.run(host='0.0.0.0', port=5000, debug=debug_mode)
