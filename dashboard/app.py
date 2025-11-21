from flask import Flask, request, jsonify, send_file, Response, render_template, redirect, url_for, session
import os
import hashlib
import json
import io
import threading
import time
import requests
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, Optional, Any
from functools import wraps
from itsdangerous import URLSafeTimedSerializer, BadSignature, SignatureExpired
from object_storage import ObjectStorageService, ObjectNotFoundError, ObjectStorageError
from models import db, Device, FirmwareVersion, User, Role, PlatformPermission, DownloadLog

app = Flask(__name__)

app.secret_key = os.environ.get("FLASK_SECRET_KEY") or "a secret key"

app.config["SQLALCHEMY_DATABASE_URI"] = os.environ.get("DATABASE_URL")
app.config["SQLALCHEMY_ENGINE_OPTIONS"] = {
    "pool_recycle": 300,
    "pool_pre_ping": True,
}

db.init_app(app)

# Token serializer for signed download URLs (24-hour expiration)
token_serializer = URLSafeTimedSerializer(app.secret_key)

# Object Storage paths (only for firmware binaries now)
FIRMWARES_PREFIX = "firmwares/"

# Local paths
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

# Platform to ESP Web Tools chip family mapping
PLATFORM_CHIP_FAMILY = {
    'esp32': 'ESP32',
    'esp32c3': 'ESP32-C3',
    'esp32s3': 'ESP32-S3',
    'esp12f': 'ESP8266',
    'esp01': 'ESP8266',
    'd1mini': 'ESP8266',
    'esp8266': 'ESP8266',
}

# In-memory cache of firmware versions (loaded from database)
LATEST_VERSIONS = {}

def load_versions_from_db():
    """Load all firmware versions from database into memory for fast lookups"""
    global LATEST_VERSIONS
    versions = {}
    for platform in SUPPORTED_PLATFORMS:
        fw = FirmwareVersion.query.filter_by(platform=platform).first()
        if fw:
            versions[platform] = fw.to_dict()
        else:
            versions[platform] = None
    
    count = sum(1 for v in versions.values() if v is not None)
    print(f"✓ Loaded {count} firmware versions from database")
    LATEST_VERSIONS = versions
    return versions

def save_version_to_db(platform, version_info):
    """Save or update a firmware version in the database"""
    fw = FirmwareVersion.query.filter_by(platform=platform).first()
    
    if fw:
        # Update existing
        fw.version = version_info['version']
        fw.filename = version_info['filename']
        fw.size = version_info['size']
        fw.sha256 = version_info['sha256']
        fw.uploaded_at = datetime.now(timezone.utc)
        fw.download_url = version_info['download_url']
        fw.storage = version_info['storage']
        fw.object_path = version_info.get('object_path')
        fw.object_name = version_info.get('object_name')
        fw.local_path = version_info.get('local_path')
    else:
        # Create new
        fw = FirmwareVersion(
            platform=platform,
            version=version_info['version'],
            filename=version_info['filename'],
            size=version_info['size'],
            sha256=version_info['sha256'],
            download_url=version_info['download_url'],
            storage=version_info['storage'],
            object_path=version_info.get('object_path'),
            object_name=version_info.get('object_name'),
            local_path=version_info.get('local_path')
        )
        db.session.add(fw)
    
    db.session.commit()
    print(f"✓ Saved {platform} v{version_info['version']} to database")

def generate_user_download_token(user_id, platform, max_age=3600):
    """Generate a user-scoped signed token for firmware downloads (default 1-hour expiration)
    
    Args:
        user_id: Database ID of the user requesting the download
        platform: Platform name (e.g., 'esp32', 'esp32c3')
        max_age: Token expiration in seconds (default 3600 = 1 hour)
    
    Returns:
        Signed token string containing user_id and platform
    """
    payload = {
        'user_id': user_id,
        'platform': platform,
        'issued_at': datetime.now(timezone.utc).isoformat()
    }
    return token_serializer.dumps(payload, salt='user-firmware-download')

def verify_user_download_token(token, max_age=3600):
    """Verify a user-scoped download token and return user_id and platform
    
    Args:
        token: Signed token string
        max_age: Maximum token age in seconds (default 3600 = 1 hour)
    
    Returns:
        Tuple of (user_id, platform) if valid, (None, None) if invalid or expired
    """
    try:
        payload = token_serializer.loads(token, salt='user-firmware-download', max_age=max_age)
        return payload.get('user_id'), payload.get('platform')
    except (BadSignature, SignatureExpired):
        return None, None

def sync_firmware_from_production():
    """Sync firmware metadata from production to dev database (dev environment only)"""
    # Only sync in dev environment
    env = os.getenv('REPL_DEPLOYMENT_TYPE', 'dev')
    if env != 'dev':
        return
    
    # Get production URL from environment
    production_url = os.getenv('PRODUCTION_API_URL')
    if not production_url:
        print("⚠ PRODUCTION_API_URL not set, skipping firmware sync from production")
        return
    
    try:
        # Query production API for firmware status (with authentication)
        headers = {}
        if DOWNLOAD_API_KEY:
            headers['X-API-Key'] = DOWNLOAD_API_KEY
        
        response = requests.get(f"{production_url}/api/status", headers=headers, timeout=10)
        response.raise_for_status()
        data = response.json()
        
        if 'firmwares' not in data:
            print("⚠ No firmware data in production response")
            return
        
        # Update local database with production firmware metadata
        synced_count = 0
        with app.app_context():
            for platform, fw_data in data['firmwares'].items():
                if fw_data is None:
                    continue
                
                # Check if we need to update (compare versions)
                local_fw = FirmwareVersion.query.filter_by(platform=platform).first()
                if local_fw and local_fw.version == fw_data.get('version'):
                    continue  # Already up to date
                
                # Save/update firmware metadata in dev database
                version_info = {
                    'version': fw_data.get('version', 'Unknown'),
                    'filename': fw_data.get('filename', ''),
                    'size': fw_data.get('size', 0),
                    'sha256': fw_data.get('checksum', ''),
                    'download_url': fw_data.get('download_url', ''),
                    'storage': fw_data.get('storage', 'object_storage'),
                    'object_path': fw_data.get('object_path'),
                    'object_name': fw_data.get('object_name'),
                    'local_path': fw_data.get('local_path')
                }
                save_version_to_db(platform, version_info)
                synced_count += 1
            
            # Reload in-memory cache
            if synced_count > 0:
                load_versions_from_db()
                print(f"✓ Synced {synced_count} firmware version(s) from production")
    
    except requests.exceptions.HTTPError as e:
        if e.response.status_code == 401:
            print(f"⚠ Production sync authentication failed (401). Check DOWNLOAD_API_KEY in config.")
        else:
            print(f"⚠ Production sync HTTP error: {e.response.status_code} - {e}")
    except requests.exceptions.RequestException as e:
        print(f"⚠ Failed to sync from production: {e}")
    except Exception as e:
        print(f"⚠ Error syncing firmware from production: {e}")

def start_production_sync_thread():
    """Start background thread that periodically syncs firmware from production (dev only)"""
    env = os.getenv('REPL_DEPLOYMENT_TYPE', 'dev')
    if env != 'dev':
        return
    
    production_url = os.getenv('PRODUCTION_API_URL')
    if not production_url:
        return
    
    def sync_loop():
        while True:
            try:
                time.sleep(300)  # 5 minutes
                sync_firmware_from_production()
            except Exception as e:
                print(f"⚠ Error in production sync thread: {e}")
                time.sleep(60)  # Wait 1 minute before retrying on error
    
    thread = threading.Thread(target=sync_loop, daemon=True)
    thread.start()
    print(f"✓ Production sync thread started (syncing every 5 minutes from {production_url})")

# Initialize database and load firmware versions
with app.app_context():
    db.create_all()
    print("✓ Database initialized")
    
    # Create admin role if it doesn't exist
    admin_role = Role.query.filter_by(name='admin').first()
    if not admin_role:
        admin_role = Role(
            name='admin',
            description='Full access to all platforms and features'
        )
        db.session.add(admin_role)
        db.session.commit()
        
        # Grant admin access to all platforms
        for platform in SUPPORTED_PLATFORMS:
            if platform not in PLATFORM_FIRMWARE_MAPPING:  # Skip aliases
                permission = PlatformPermission(
                    role_id=admin_role.id,
                    platform=platform,
                    can_download=True
                )
                db.session.add(permission)
        db.session.commit()
        print(f"✓ Created admin role with access to {len(SUPPORTED_PLATFORMS) - len(PLATFORM_FIRMWARE_MAPPING)} platforms")
    else:
        print(f"✓ Found admin role with {len(admin_role.platform_permissions)} platform permissions")
    
    # Create initial user if none exists
    if User.query.count() == 0:
        initial_user = User(
            email='julian@norrtek.se',
            role_id=admin_role.id
        )
        initial_user.set_password('testing123')
        db.session.add(initial_user)
        db.session.commit()
        print("✓ Created initial user: julian@norrtek.se (admin)")
    else:
        print(f"✓ Found {User.query.count()} user(s) in database")
    
    # Load firmware versions from database (must be inside app context)
    load_versions_from_db()
    
    # Sync firmware from production on startup (dev environment only)
    sync_firmware_from_production()

from mqtt_subscriber import start_mqtt_subscriber, set_app_context

# Pass app context to MQTT subscriber for database operations
set_app_context(app)

# Start MQTT subscriber in background thread (must be after db init)
mqtt_client = start_mqtt_subscriber()

# Start production sync thread (dev environment only)
start_production_sync_thread()

# Authentication decorator
def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if not session.get('logged_in'):
            return redirect(url_for('login', next=request.url))
        return f(*args, **kwargs)
    return decorated_function

# Authentication routes
@app.route('/login', methods=['GET', 'POST'])
def login():
    error = None
    
    if request.method == 'POST':
        email = request.form.get('username')  # Form field is still named 'username' for consistency
        password = request.form.get('password')
        
        # Check credentials against database
        user = User.query.filter_by(email=email).first()
        
        if user and user.check_password(password):
            session['logged_in'] = True
            session['user_email'] = user.email
            session['user_id'] = user.id
            
            # Redirect to requested page or dashboard
            next_page = request.args.get('next')
            if next_page:
                return redirect(next_page)
            return redirect(url_for('index'))
        else:
            error = 'Invalid credentials. Please try again.'
    
    return render_template('login.html', error=error)

@app.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('login'))

# Protected routes
@app.route('/')
@login_required
def index():
    return render_template('index.html')

@app.route('/ota-history')
@login_required
def ota_history():
    """OTA update history page with filters"""
    return render_template('ota_history.html')

@app.route('/api')
def api_index():
    return jsonify({
        'service': 'Ski Clock Firmware Server',
        'status': 'running',
        'supported_platforms': SUPPORTED_PLATFORMS,
        'endpoints': {
            'download': '/api/firmware/<platform>',
            'upload': '/api/upload (POST)',
            'status': '/api/status'
        }
    })

@app.route('/firmware/manifest/<platform>.json')
@login_required
def firmware_manifest(platform):
    """Generate ESP Web Tools manifest for browser-based flashing (requires login and platform permission)"""
    platform = platform.lower()
    original_platform = platform
    
    # Apply platform firmware mapping for backward compatibility
    if platform in PLATFORM_FIRMWARE_MAPPING:
        platform = PLATFORM_FIRMWARE_MAPPING[platform]
    
    # Validate original platform request (before mapping) OR mapped platform
    if original_platform not in SUPPORTED_PLATFORMS and platform not in SUPPORTED_PLATFORMS:
        return jsonify({'error': 'Invalid platform'}), 400
    
    # Get user from session and check permissions BEFORE generating manifest
    user_id = session.get('user_id')
    user = User.query.get(user_id)
    
    if not user:
        return jsonify({'error': 'User not found'}), 401
    
    # Check if user has permission to download this platform
    if not user.can_download_platform(platform):
        return jsonify({'error': f'You do not have permission to access {platform} firmware'}), 403
    
    version_info = LATEST_VERSIONS.get(platform)
    
    if not version_info:
        return jsonify({'error': 'No firmware available'}), 404
    
    # Get chip family for this platform
    chip_family = PLATFORM_CHIP_FAMILY.get(platform, 'ESP32')
    
    # Generate user-scoped download token for the manifest (user already has permission)
    download_token = generate_user_download_token(user_id, platform, max_age=3600)
    firmware_url = url_for('user_download_firmware', token=download_token, _external=True)
    
    # Create ESP Web Tools manifest
    manifest = {
        "name": f"Ski Clock Neo - {platform.upper()}",
        "version": version_info['version'],
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": chip_family,
                "parts": [
                    {
                        "path": firmware_url,
                        "offset": 0
                    }
                ]
            }
        ]
    }
    
    # Return with CORS headers for ESP Web Tools
    response = jsonify(manifest)
    response.headers['Access-Control-Allow-Origin'] = '*'
    response.headers['Access-Control-Allow-Methods'] = 'GET, OPTIONS'
    response.headers['Access-Control-Allow-Headers'] = 'Content-Type'
    
    return response

@app.route('/firmware/user-download/<token>')
def user_download_firmware(token):
    """User-scoped firmware download endpoint using user-specific signed tokens"""
    # Verify user token and extract user_id and platform
    user_id, platform = verify_user_download_token(token)
    
    if not user_id or not platform:
        return jsonify({'error': 'Invalid or expired download token'}), 401
    
    # Load user from database
    user = User.query.get(user_id)
    if not user:
        return jsonify({'error': 'User not found'}), 401
    
    # Check if user has permission to download this platform
    if not user.can_download_platform(platform):
        return jsonify({'error': f'You do not have permission to download {platform} firmware'}), 403
    
    # Validate platform
    if platform not in SUPPORTED_PLATFORMS:
        return jsonify({'error': 'Invalid platform'}), 400
    
    version_info = LATEST_VERSIONS.get(platform)
    
    if not version_info:
        return jsonify({'error': 'No firmware available'}), 404
    
    # Log the download
    download_log = DownloadLog(
        user_id=user_id,
        platform=platform,
        firmware_version=version_info.get('version'),
        ip_address=request.remote_addr,
        user_agent=request.headers.get('User-Agent')
    )
    db.session.add(download_log)
    db.session.commit()
    
    # Check where the firmware is stored
    storage_location = version_info.get('storage', 'local')
    
    if storage_location == 'object_storage':
        # Download from Object Storage
        object_path = None
        try:
            storage = ObjectStorageService()
            
            # Use the full object_path if available
            object_path = version_info.get('object_path')
            if not object_path:
                bucket_name = storage.get_bucket_name()
                object_path = f"/{bucket_name}/{version_info.get('object_name', FIRMWARES_PREFIX + version_info['filename'])}"
            
            # Download as bytes and stream to client
            firmware_data = storage.download_as_bytes(object_path)
            
            response = Response(
                firmware_data,
                mimetype='application/octet-stream',
                headers={
                    'Content-Disposition': f'attachment; filename="{version_info["filename"]}"',
                    'Content-Length': str(len(firmware_data)),
                    'Access-Control-Allow-Origin': '*',
                    'Access-Control-Allow-Methods': 'GET, OPTIONS',
                    'Access-Control-Allow-Headers': 'Content-Type'
                }
            )
            return response
        except (ObjectStorageError, ObjectNotFoundError) as e:
            error_msg = f"Error downloading from Object Storage"
            if object_path:
                error_msg += f" ({object_path})"
            print(f"{error_msg}: {e}")
            # Try local filesystem as fallback
            firmware_path = FIRMWARES_DIR / version_info['filename']
            if firmware_path.exists():
                response = send_file(
                    firmware_path,
                    mimetype='application/octet-stream',
                    as_attachment=True,
                    download_name=version_info['filename']
                )
                response.headers['Access-Control-Allow-Origin'] = '*'
                response.headers['Access-Control-Allow-Methods'] = 'GET, OPTIONS'
                response.headers['Access-Control-Allow-Headers'] = 'Content-Type'
                return response
            return jsonify({'error': 'Firmware file not found'}), 404
    else:
        # Serve from local filesystem
        firmware_path = FIRMWARES_DIR / version_info['filename']
        if not firmware_path.exists():
            return jsonify({'error': 'Firmware file not found'}), 404
        
        response = send_file(
            firmware_path,
            mimetype='application/octet-stream',
            as_attachment=True,
            download_name=version_info['filename']
        )
        response.headers['Access-Control-Allow-Origin'] = '*'
        response.headers['Access-Control-Allow-Methods'] = 'GET, OPTIONS'
        response.headers['Access-Control-Allow-Headers'] = 'Content-Type'
        return response

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
    
    # Update in-memory cache
    LATEST_VERSIONS[platform] = version_info
    
    # Save to database
    save_version_to_db(platform, version_info)
    
    # Mirror esp12f firmware to legacy esp8266 alias for monitoring visibility
    if platform == 'esp12f':
        esp8266_info = version_info.copy()
        esp8266_info['download_url'] = '/api/firmware/esp8266'
        LATEST_VERSIONS['esp8266'] = esp8266_info
        save_version_to_db('esp8266', esp8266_info)
    
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
@login_required
def status():
    # Get user from session
    user_id = session.get('user_id')
    user = User.query.get(user_id)
    
    if not user:
        return jsonify({'error': 'User not found'}), 401
    
    # Generate fresh user-scoped download tokens ONLY for platforms the user has permission for
    firmwares_with_tokens = {}
    for platform, fw_data in LATEST_VERSIONS.items():
        if fw_data:
            # Check if user has permission to download this platform
            if user.can_download_platform(platform):
                fw_copy = fw_data.copy()
                # Generate fresh user-scoped token valid for 1 hour
                token = generate_user_download_token(user_id, platform, max_age=3600)
                fw_copy['public_download_url'] = f'/firmware/user-download/{token}'
                fw_copy['can_download'] = True
                firmwares_with_tokens[platform] = fw_copy
            else:
                # Include firmware metadata but no download token
                fw_copy = fw_data.copy()
                fw_copy['can_download'] = False
                fw_copy['public_download_url'] = None
                firmwares_with_tokens[platform] = fw_copy
        else:
            firmwares_with_tokens[platform] = None
    
    return jsonify({
        'firmwares': firmwares_with_tokens,
        'storage': {
            'files': [f.name for f in FIRMWARES_DIR.glob('*.bin')],
            'count': len(list(FIRMWARES_DIR.glob('*.bin')))
        },
        'config_source': 'file' if CONFIG_FILE.exists() else 'environment'
    })

@app.route('/api/devices')
@login_required
def devices():
    """Get all devices with online/offline status (15 minute threshold)"""
    all_devices = Device.query.all()
    device_list = [device.to_dict(online_threshold_minutes=15) for device in all_devices]
    
    # Sort by online status (online first) then by last seen (newest first)
    device_list.sort(key=lambda d: (not d['online'], d['last_seen']), reverse=True)
    
    online_count = sum(1 for d in device_list if d['online'])
    
    return jsonify({
        'devices': device_list,
        'count': len(device_list),
        'online_count': online_count,
        'offline_count': len(device_list) - online_count,
        'mqtt_enabled': mqtt_client is not None
    })

@app.route('/api/devices/<device_id>', methods=['DELETE'])
@login_required
def delete_device(device_id):
    """Delete a device from the database (for decommissioned devices)"""
    device = Device.query.filter_by(device_id=device_id).first()
    
    if not device:
        return jsonify({'error': 'Device not found'}), 404
    
    db.session.delete(device)
    db.session.commit()
    
    print(f"🗑️  Device removed: {device_id} ({device.board_type})")
    
    return jsonify({
        'success': True,
        'message': f'Device {device_id} removed successfully'
    }), 200

@app.route('/api/ota-logs')
@login_required
def ota_logs():
    """Get OTA update logs with optional filters"""
    from models import OTAUpdateLog
    from sqlalchemy import desc, and_
    from datetime import datetime, timedelta
    
    # Get query parameters
    device_id = request.args.get('device_id')
    status = request.args.get('status')
    start_date = request.args.get('start_date')  # ISO 8601 format
    end_date = request.args.get('end_date')  # ISO 8601 format
    limit = request.args.get('limit', type=int, default=100)
    offset = request.args.get('offset', type=int, default=0)
    
    # Build query
    query = OTAUpdateLog.query
    
    # Apply filters
    if device_id:
        query = query.filter_by(device_id=device_id)
    if status:
        query = query.filter_by(status=status)
    
    # Date range filters
    if start_date:
        try:
            start_dt = datetime.fromisoformat(start_date.replace('Z', '+00:00'))
            query = query.filter(OTAUpdateLog.started_at >= start_dt)
        except (ValueError, AttributeError):
            return jsonify({'error': 'Invalid start_date format. Use ISO 8601.'}), 400
    
    if end_date:
        try:
            end_dt = datetime.fromisoformat(end_date.replace('Z', '+00:00'))
            query = query.filter(OTAUpdateLog.started_at <= end_dt)
        except (ValueError, AttributeError):
            return jsonify({'error': 'Invalid end_date format. Use ISO 8601.'}), 400
    
    # Get total count before pagination
    total_count = query.count()
    
    # Order by most recent first, apply pagination
    logs = query.order_by(desc(OTAUpdateLog.started_at)).limit(limit).offset(offset).all()
    
    return jsonify({
        'logs': [log.to_dict() for log in logs],
        'count': len(logs),
        'total': total_count,
        'offset': offset,
        'limit': limit
    })

@app.route('/api/ota-progress')
@login_required
def ota_progress():
    """Get in-progress OTA updates for all devices"""
    from models import OTAUpdateLog
    
    # Get all in-progress updates (started or downloading)
    in_progress = OTAUpdateLog.query.filter(
        OTAUpdateLog.status.in_(['started', 'downloading'])
    ).all()
    
    # Create a map of device_id -> progress info
    progress_map = {}
    for log in in_progress:
        progress_map[log.device_id] = {
            'session_id': log.session_id,
            'platform': log.platform,
            'old_version': log.old_version,
            'new_version': log.new_version,
            'status': log.status,
            'progress': log.download_progress,
            'started_at': log.started_at.isoformat() if log.started_at else None
        }
    
    return jsonify(progress_map)

@app.route('/api/ota-stats')
@login_required
def ota_stats():
    """Get OTA update statistics"""
    from models import OTAUpdateLog
    from sqlalchemy import func
    
    # Total updates
    total_updates = OTAUpdateLog.query.count()
    
    # Success/failure counts
    success_count = OTAUpdateLog.query.filter_by(status='success').count()
    failed_count = OTAUpdateLog.query.filter_by(status='failed').count()
    in_progress_count = OTAUpdateLog.query.filter(
        OTAUpdateLog.status.in_(['started', 'downloading'])
    ).count()
    
    # Success rate
    success_rate = (success_count / total_updates * 100) if total_updates > 0 else 0
    
    # Average update duration (only for completed updates)
    completed_logs = OTAUpdateLog.query.filter(
        OTAUpdateLog.completed_at.isnot(None)
    ).all()
    
    if completed_logs:
        durations = [
            (log.completed_at - log.started_at).total_seconds()
            for log in completed_logs
        ]
        avg_duration = sum(durations) / len(durations)
    else:
        avg_duration = 0
    
    # Recent updates (last 7 days)
    from datetime import timedelta
    seven_days_ago = datetime.now(timezone.utc) - timedelta(days=7)
    recent_updates = OTAUpdateLog.query.filter(
        OTAUpdateLog.started_at >= seven_days_ago
    ).count()
    
    # Failed devices (unique devices with failed updates in last 24 hours)
    one_day_ago = datetime.now(timezone.utc) - timedelta(days=1)
    failed_devices = OTAUpdateLog.query.filter(
        OTAUpdateLog.status == 'failed',
        OTAUpdateLog.started_at >= one_day_ago
    ).with_entities(OTAUpdateLog.device_id).distinct().count()
    
    return jsonify({
        'total_updates': total_updates,
        'success_count': success_count,
        'failed_count': failed_count,
        'in_progress_count': in_progress_count,
        'success_rate': round(success_rate, 1),
        'avg_duration_seconds': round(avg_duration, 1),
        'recent_updates_7days': recent_updates,
        'failed_devices_24h': failed_devices
    })

if __name__ == '__main__':
    # Development mode - debug enabled
    # Production deployments use gunicorn instead
    import os
    debug_mode = os.getenv('FLASK_ENV') == 'development'
    app.run(host='0.0.0.0', port=5000, debug=debug_mode)
