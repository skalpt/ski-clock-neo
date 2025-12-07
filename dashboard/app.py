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
from models import db, Device, FirmwareVersion, User, Role, PlatformPermission, DownloadLog, DevEnvironment

# Map device board types (as reported by firmware) to platform identifiers
# This must stay in sync with BOARD_TYPE_TO_PLATFORM in mqtt_subscriber.py
BOARD_TYPE_TO_PLATFORM = {
    'ESP32': 'esp32',
    'ESP32-C3': 'esp32c3',
    'ESP32-S3': 'esp32s3',
    'ESP-12F': 'esp12f',
    'ESP-01': 'esp01',
    'Wemos D1 Mini': 'd1mini',
}

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
FIRMWARES_PREFIX_BASE = "firmwares/"

def get_firmwares_prefix():
    """Get the environment-specific firmwares prefix for object storage.
    
    Returns:
        'firmwares/dev/' for development environments
        'firmwares/prod/' for production environments
    """
    env_scope = get_dashboard_environment_scope()
    return f"{FIRMWARES_PREFIX_BASE}{env_scope}/"

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

# Environment detection (fail-closed: defaults to production)
def is_production_environment():
    """Detect environment with fail-closed security posture
    
    Returns False (development) ONLY if:
    - DEV_MODE='true' (explicit development flag)
    - SAFE_CONFIG_BYPASS='true' (emergency override)
    - REPL_ID set but REPLIT_DEPLOYMENT not set (Replit workspace, not deployed)
    - REPLIT_DEPLOYMENT in {'staging', 'qa', 'dev'} (known non-prod deployment)
    
    Otherwise returns True (production), enforcing secure configuration
    for Docker, bare metal, and unknown cloud deployments.
    """
    # Emergency bypass for troubleshooting
    if os.getenv('SAFE_CONFIG_BYPASS', '').lower() == 'true':
        return False
    
    # Explicit development mode
    if os.getenv('DEV_MODE', '').lower() == 'true':
        return False
    
    # Replit workspace (not a deployment) - REPL_ID exists but no REPLIT_DEPLOYMENT
    # REPLIT_DEPLOYMENT is set to "1" when deployed
    if os.getenv('REPL_ID') and not os.getenv('REPLIT_DEPLOYMENT'):
        return False
    
    # Known non-production deployment types (if Replit adds more granular types)
    deployment_type = os.getenv('REPLIT_DEPLOYMENT', '').lower()
    if deployment_type in {'staging', 'qa', 'dev'}:
        return False
    
    # Default to production (fail closed) for all other environments
    # This includes REPLIT_DEPLOYMENT="1" (published apps)
    return True

def get_dashboard_environment_scope():
    """Get the environment scope for this dashboard instance.
    
    Returns 'prod' for production deployments, 'dev' for development.
    Used to filter devices and MQTT topics to ensure strict environment separation.
    """
    return 'prod' if is_production_environment() else 'dev'

# Production configuration validation
def validate_production_config():
    """Enforce secure configuration in production environments"""
    is_prod = is_production_environment()
    
    # Check for insecure defaults
    warnings = []
    
    if app.secret_key == "a secret key":
        warnings.append("FLASK_SECRET_KEY is using insecure default")
    
    if UPLOAD_API_KEY == 'dev-key-change-in-production':
        warnings.append("UPLOAD_API_KEY is using development default")
    
    if DOWNLOAD_API_KEY == 'dev-download-key':
        warnings.append("DOWNLOAD_API_KEY is using development default")
    
    if warnings:
        if is_prod:
            # In production, fail hard to prevent insecure deployments
            error_msg = "PRODUCTION CONFIGURATION ERROR:\n" + "\n".join(f"  - {issue}" for issue in warnings)
            print(f"\n{'='*60}")
            print(error_msg)
            print(f"{'='*60}\n")
            raise RuntimeError(error_msg + "\n\nSet proper values via Secrets or environment variables.")
        else:
            # In development/staging, just warn
            for warning in warnings:
                print(f"⚠ {warning} (development only)")
    else:
        env_type = "production" if is_prod else "development"
        print(f"✓ Configuration validated successfully ({env_type})")

validate_production_config()

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

# Canonical platforms (platforms with database rows, excluding aliases)
CANONICAL_PLATFORMS = [
    'esp32',
    'esp32c3',
    'esp32s3',
    'esp12f',
    'esp01',
    'd1mini'
    # NOTE: 'esp8266' is NOT canonical - it's an alias to esp12f
]

# Helper functions for alias management
def get_aliases_for_platform(canonical_platform):
    """Get all alias platforms that map to a canonical platform
    
    Args:
        canonical_platform: Canonical platform name (e.g., 'esp12f')
    
    Returns:
        List of alias platform names (e.g., ['esp8266'])
    """
    aliases = []
    for alias, target in PLATFORM_FIRMWARE_MAPPING.items():
        if target == canonical_platform and alias not in CANONICAL_PLATFORMS:
            aliases.append(alias)
    return aliases

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
# Key format: (product, platform) tuple
LATEST_VERSIONS = {}

def _cache_key(product: str, platform: str) -> tuple:
    """Generate cache key from product and platform"""
    return (product, platform)

def refresh_firmware_cache(platform: str, product: str):
    """Refresh cache for a specific product/platform and all its aliases
    
    Loads the LATEST firmware version from database and updates LATEST_VERSIONS cache
    for both the canonical platform and all mapped aliases.
    
    Args:
        platform: Canonical platform name (e.g., 'esp32c3', 'esp12f')
        product: Product name (e.g., 'ski-clock-neo')
    """
    global LATEST_VERSIONS
    
    # Load latest version from database (ordered by uploaded_at descending)
    fw = FirmwareVersion.query.filter_by(
        product=product, 
        platform=platform
    ).order_by(FirmwareVersion.uploaded_at.desc()).first()
    
    key = _cache_key(product, platform)
    
    if not fw:
        # Clear canonical platform and all aliases when firmware is missing
        LATEST_VERSIONS[key] = None
        for alias in get_aliases_for_platform(platform):
            LATEST_VERSIONS[_cache_key(product, alias)] = None
        return
    
    # Update canonical platform
    version_info = fw.to_dict()
    LATEST_VERSIONS[key] = version_info
    
    # Update all aliases pointing to this platform dynamically
    for alias in get_aliases_for_platform(platform):
        alias_info = version_info.copy()
        alias_info['platform'] = alias
        alias_info['download_url'] = f'/api/firmware/{alias}?product={product}'
        LATEST_VERSIONS[_cache_key(product, alias)] = alias_info

def refresh_all_firmware_cache(product: str = None):
    """Refresh cache for all canonical platforms (and their aliases)
    
    Args:
        product: If specified, only refresh for this product. Otherwise refresh all products.
    """
    if product:
        for platform in CANONICAL_PLATFORMS:
            refresh_firmware_cache(platform, product)
    else:
        # Get all unique products from database
        products = db.session.query(FirmwareVersion.product).distinct().all()
        for (prod,) in products:
            for platform in CANONICAL_PLATFORMS:
                refresh_firmware_cache(platform, prod)

def get_firmware_version(platform: str, product: str) -> Optional[Dict[str, Any]]:
    """
    Get firmware version with automatic DB fallback.
    Checks cache first, falls back to database if cache is empty or platform not found.
    
    Args:
        platform: Platform name (e.g., 'esp32c3', 'esp12f')
        product: Product name (required for multi-product support)
    """
    key = _cache_key(product, platform)
    
    # Try cache first
    version_info = LATEST_VERSIONS.get(key)
    
    # Cache miss - refresh from database
    if version_info is None:
        # Resolve alias to canonical platform using PLATFORM_FIRMWARE_MAPPING
        canonical_platform = PLATFORM_FIRMWARE_MAPPING.get(platform, platform)
        
        # Refresh cache for canonical platform (and all its aliases)
        if canonical_platform in CANONICAL_PLATFORMS:
            refresh_firmware_cache(canonical_platform, product)
            version_info = LATEST_VERSIONS.get(key)
    
    return version_info

def load_versions_from_db():
    """Load all firmware versions from database into memory for fast lookups"""
    # Use centralized cache refresh to ensure aliases are updated
    refresh_all_firmware_cache()
    
    # Count unique product/platform combinations loaded
    count = sum(1 for key, val in LATEST_VERSIONS.items() if val is not None)
    print(f"✓ Loaded {count} firmware versions from database")
    return LATEST_VERSIONS

def save_version_to_db(platform: str, version_info: dict, product: str, is_sync: bool = False):
    """Save or update a firmware version in the database.
    
    Now supports version history: each product+platform+version combination is unique.
    If the same version is re-uploaded, it updates the existing record.
    New versions create new records, preserving history.
    
    Args:
        platform: Platform name (e.g., 'esp32c3')
        version_info: Dictionary with version details
        product: Product name (required for multi-product support)
        is_sync: If True, preserves existing uploaded_at on updates (for production sync)
    """
    version = version_info['version']
    
    # Check for existing product+platform+version combination
    fw = FirmwareVersion.query.filter_by(product=product, platform=platform, version=version).first()
    
    if fw:
        # Update existing version (re-upload of same version)
        fw.filename = version_info['filename']
        fw.size = version_info['size']
        fw.sha256 = version_info['sha256']
        # Only update uploaded_at on actual uploads, not syncs
        if not is_sync:
            fw.uploaded_at = datetime.now(timezone.utc)
        fw.download_url = version_info['download_url']
        fw.storage = version_info['storage']
        fw.object_path = version_info.get('object_path')
        fw.object_name = version_info.get('object_name')
        fw.local_path = version_info.get('local_path')
        
        # Update bootloader fields if provided
        fw.bootloader_filename = version_info.get('bootloader_filename')
        fw.bootloader_size = version_info.get('bootloader_size')
        fw.bootloader_sha256 = version_info.get('bootloader_sha256')
        fw.bootloader_object_path = version_info.get('bootloader_object_path')
        fw.bootloader_object_name = version_info.get('bootloader_object_name')
        fw.bootloader_local_path = version_info.get('bootloader_local_path')
        
        # Update partitions fields if provided
        fw.partitions_filename = version_info.get('partitions_filename')
        fw.partitions_size = version_info.get('partitions_size')
        fw.partitions_sha256 = version_info.get('partitions_sha256')
        fw.partitions_object_path = version_info.get('partitions_object_path')
        fw.partitions_object_name = version_info.get('partitions_object_name')
        fw.partitions_local_path = version_info.get('partitions_local_path')
        
        # Update description if provided
        if version_info.get('description'):
            fw.description = version_info.get('description')
        print(f"✓ Updated existing {product}/{platform} v{version} in database")
    else:
        # Parse uploaded_at from version_info if provided (from production sync)
        # Falls back to model default (current time) if not provided
        uploaded_at = None
        if version_info.get('uploaded_at'):
            try:
                uploaded_at = datetime.fromisoformat(version_info['uploaded_at'].replace('Z', '+00:00'))
            except (ValueError, AttributeError):
                uploaded_at = None  # Use model default
        
        # Create new version record (preserves history)
        fw = FirmwareVersion(
            product=product,
            platform=platform,
            version=version,
            filename=version_info['filename'],
            size=version_info['size'],
            sha256=version_info['sha256'],
            download_url=version_info['download_url'],
            storage=version_info['storage'],
            object_path=version_info.get('object_path'),
            object_name=version_info.get('object_name'),
            local_path=version_info.get('local_path'),
            
            # Bootloader fields
            bootloader_filename=version_info.get('bootloader_filename'),
            bootloader_size=version_info.get('bootloader_size'),
            bootloader_sha256=version_info.get('bootloader_sha256'),
            bootloader_object_path=version_info.get('bootloader_object_path'),
            bootloader_object_name=version_info.get('bootloader_object_name'),
            bootloader_local_path=version_info.get('bootloader_local_path'),
            
            # Partitions fields
            partitions_filename=version_info.get('partitions_filename'),
            partitions_size=version_info.get('partitions_size'),
            partitions_sha256=version_info.get('partitions_sha256'),
            partitions_object_path=version_info.get('partitions_object_path'),
            partitions_object_name=version_info.get('partitions_object_name'),
            partitions_local_path=version_info.get('partitions_local_path'),
            
            # Build description (optional)
            description=version_info.get('description')
        )
        # Set uploaded_at if parsed from production, otherwise use model default
        if uploaded_at:
            fw.uploaded_at = uploaded_at
        db.session.add(fw)
        print(f"✓ Added new {product}/{platform} v{version} to database")
    
    db.session.commit()
    
    # Refresh cache for this product/platform and all aliases
    refresh_firmware_cache(platform, product)

def _sync_single_firmware(product: str, platform: str, fw_data: dict) -> int:
    """Helper function to sync a single firmware entry from production.
    
    Returns 1 if synced, 0 if already up to date.
    """
    local_fw = FirmwareVersion.query.filter_by(product=product, platform=platform).first()
    if local_fw and local_fw.version == fw_data.get('version'):
        return 0
    
    version_info = {
        'version': fw_data.get('version', 'Unknown'),
        'filename': fw_data.get('filename', ''),
        'size': fw_data.get('size', 0),
        'sha256': fw_data.get('sha256', ''),
        'download_url': fw_data.get('download_url', ''),
        'storage': fw_data.get('storage', 'object_storage'),
        'object_path': fw_data.get('object_path'),
        'object_name': fw_data.get('object_name'),
        'local_path': fw_data.get('local_path'),
        'uploaded_at': fw_data.get('uploaded_at'),  # Preserve original upload timestamp from production
        
        'bootloader_filename': fw_data.get('bootloader', {}).get('filename') if fw_data.get('bootloader') else None,
        'bootloader_size': fw_data.get('bootloader', {}).get('size') if fw_data.get('bootloader') else None,
        'bootloader_sha256': fw_data.get('bootloader', {}).get('sha256') if fw_data.get('bootloader') else None,
        
        'partitions_filename': fw_data.get('partitions', {}).get('filename') if fw_data.get('partitions') else None,
        'partitions_size': fw_data.get('partitions', {}).get('size') if fw_data.get('partitions') else None,
        'partitions_sha256': fw_data.get('partitions', {}).get('sha256') if fw_data.get('partitions') else None,
        
        'description': fw_data.get('description')
    }
    save_version_to_db(platform, version_info, product, is_sync=True)
    return 1

def sync_firmware_from_production():
    """Sync firmware metadata from production to dev database (dev environment only)"""
    # Only sync in dev environment (workspace, not deployed)
    if is_production_environment():
        return
    
    production_url = os.getenv('PRODUCTION_API_URL')
    if not production_url:
        print("⚠ PRODUCTION_API_URL not set, skipping firmware sync from production")
        return
    
    try:
        headers = {}
        if DOWNLOAD_API_KEY:
            headers['X-API-Key'] = DOWNLOAD_API_KEY
        
        response = requests.get(f"{production_url}/api/firmware-metadata", headers=headers, timeout=10)
        response.raise_for_status()
        data = response.json()
        
        if 'firmwares' not in data:
            print("⚠ No firmware data in production response")
            return
        
        synced_count = 0
        with app.app_context():
            for product, platforms in data['firmwares'].items():
                if platforms is None or not isinstance(platforms, dict):
                    continue
                
                for platform, fw_data in platforms.items():
                    if platform not in CANONICAL_PLATFORMS:
                        continue
                    if fw_data is None or not isinstance(fw_data, dict):
                        continue
                    if 'version' not in fw_data:
                        continue
                    
                    synced_count += _sync_single_firmware(product, platform, fw_data)
            
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
    # Only sync in dev environment (workspace, not deployed)
    if is_production_environment():
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
                time.sleep(60)
    
    thread = threading.Thread(target=sync_loop, daemon=True)
    thread.start()
    print("✓ Started production sync thread (every 5 minutes)")


def register_with_production():
    """Register this dev environment with production for GitHub Actions proxy (dev only)
    
    Called on startup in dev environments to register the current URL with production.
    Production stores this URL and uses it to proxy firmware uploads from GitHub Actions.
    """
    # Only register in dev environment
    if is_production_environment():
        return
    
    production_url = os.getenv('PRODUCTION_API_URL')
    if not production_url:
        print("⚠ PRODUCTION_API_URL not set, skipping dev registration with production")
        return
    
    # Get current dev URL from Replit environment
    dev_domain = os.getenv('REPLIT_DEV_DOMAIN')
    if not dev_domain:
        print("⚠ REPLIT_DEV_DOMAIN not set, skipping dev registration")
        return
    
    dev_url = f"https://{dev_domain}"
    
    try:
        response = requests.post(
            f"{production_url}/api/dev/register",
            headers={
                'X-API-Key': UPLOAD_API_KEY,
                'Content-Type': 'application/json'
            },
            json={
                'base_url': dev_url,
                'auth_token': UPLOAD_API_KEY,
                'name': 'default'
            },
            timeout=30
        )
        
        if response.ok:
            print(f"✓ Registered with production: {dev_url}")
        else:
            print(f"⚠ Failed to register with production: {response.status_code} - {response.text}")
    except requests.exceptions.RequestException as e:
        print(f"⚠ Failed to reach production for registration: {e}")
    except Exception as e:
        print(f"⚠ Error registering with production: {e}")


def start_dev_heartbeat_thread():
    """Start background thread that sends periodic heartbeats to production (dev only)"""
    if is_production_environment():
        return
    
    production_url = os.getenv('PRODUCTION_API_URL')
    if not production_url:
        return
    
    def heartbeat_loop():
        while True:
            try:
                time.sleep(60)  # Send heartbeat every minute
                
                response = requests.post(
                    f"{production_url}/api/dev/heartbeat",
                    headers={
                        'X-API-Key': UPLOAD_API_KEY,
                        'Content-Type': 'application/json'
                    },
                    json={'name': 'default'},
                    timeout=10
                )
                
                if not response.ok:
                    # Re-register if heartbeat fails
                    register_with_production()
            except Exception as e:
                print(f"⚠ Dev heartbeat failed: {e}")
                time.sleep(30)
    
    thread = threading.Thread(target=heartbeat_loop, daemon=True)
    thread.start()
    print("✓ Started dev heartbeat thread (every 60 seconds)")


def generate_user_download_token(user_id, platform, product, max_age=3600, version=None):
    """Generate a user-scoped signed token for firmware downloads (default 1-hour expiration)
    
    Args:
        user_id: Database ID of the user requesting the download
        platform: Platform name (e.g., 'esp32', 'esp32c3')
        product: Product name (e.g., 'ski-clock-neo')
        max_age: Token expiration in seconds (default 3600 = 1 hour)
        version: Optional specific firmware version (defaults to latest if None)
    
    Returns:
        Signed token string containing user_id, platform, product, and optionally version
    """
    payload = {
        'user_id': user_id,
        'platform': platform,
        'product': product,
        'issued_at': datetime.now(timezone.utc).isoformat()
    }
    if version:
        payload['version'] = version
    return token_serializer.dumps(payload, salt='user-firmware-download')

def verify_user_download_token(token, max_age=3600):
    """Verify a user-scoped download token and return user_id, platform, product, and optional version
    
    Args:
        token: Signed token string
        max_age: Maximum token age in seconds (default 3600 = 1 hour)
    
    Returns:
        Tuple of (user_id, platform, product, version) if valid, (None, None, None, None) if invalid or expired
        version may be None if token was for latest version
    """
    try:
        payload = token_serializer.loads(token, salt='user-firmware-download', max_age=max_age)
        return payload.get('user_id'), payload.get('platform'), payload.get('product'), payload.get('version')
    except (BadSignature, SignatureExpired):
        return None, None, None, None

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
    
    # Check user count
    user_count = User.query.count()
    if user_count == 0:
        print("⚠ No users in database - create an admin user manually")
    else:
        print(f"✓ Found {user_count} user(s) in database")
    
    # Load firmware versions from database (must be inside app context)
    load_versions_from_db()
    
    # Sync firmware from production on startup (dev environment only)
    sync_firmware_from_production()

from mqtt_subscriber import start_mqtt_subscriber, stop_mqtt_subscriber, set_app_context
import atexit

# Pass app context to MQTT subscriber for database operations
set_app_context(app)

# Start MQTT subscriber in background thread (must be after db init)
mqtt_client = start_mqtt_subscriber()

# Start production sync thread (dev environment only)
start_production_sync_thread()

# Register dev environment with production and start heartbeat thread (dev only)
register_with_production()
start_dev_heartbeat_thread()

# Register cleanup handler to stop MQTT subscriber on app shutdown
atexit.register(stop_mqtt_subscriber)

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
        user = db.session.execute(db.select(User).filter_by(email=email)).scalar_one_or_none()
        
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

@app.route('/history')
@login_required
def history():
    """Unified history page with tabs for Snapshots, OTA Updates, and Events"""
    return render_template('history.html')

@app.route('/api')
def api_index():
    return jsonify({
        'service': 'Norrtek IoT Firmware Server',
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
    """Generate ESP Web Tools manifest for browser-based flashing (requires login and platform permission)
    
    Query parameters:
    - product: product name (required)
    - mode: 'quick' (firmware only, default) or 'full' (bootloader + partitions + firmware)
    - version: specific version to flash (optional, defaults to latest)
    """
    platform = platform.lower()
    original_platform = platform
    product = request.args.get('product')
    flash_mode = request.args.get('mode', 'quick').lower()  # Default to quick flash
    requested_version = request.args.get('version')  # Specific version to flash (optional)
    
    # Validate required product parameter
    if not product:
        return jsonify({'error': 'Missing required parameter: product'}), 400
    
    # Validate flash mode
    if flash_mode not in ['quick', 'full']:
        return jsonify({'error': 'Invalid flash mode. Use "quick" or "full"'}), 400
    
    # Apply platform firmware mapping
    if platform in PLATFORM_FIRMWARE_MAPPING:
        platform = PLATFORM_FIRMWARE_MAPPING[platform]
    
    # Validate original platform request (before mapping) OR mapped platform
    if original_platform not in SUPPORTED_PLATFORMS and platform not in SUPPORTED_PLATFORMS:
        return jsonify({'error': 'Invalid platform'}), 400
    
    # Get user from session and check permissions BEFORE generating manifest
    user_id = session.get('user_id')
    user = db.session.get(User, user_id)
    
    if not user:
        return jsonify({'error': 'User not found'}), 401
    
    # Check if user has permission to download this platform
    if not user.can_download_platform(platform):
        return jsonify({'error': f'You do not have permission to access {platform} firmware'}), 403
    
    # Get firmware version info - either specific version or latest
    if requested_version:
        # Look up specific version from database
        fw = FirmwareVersion.query.filter_by(product=product, platform=platform, version=requested_version).first()
        if not fw:
            return jsonify({'error': f'Version {requested_version} not found for {product}/{platform}'}), 404
        version_info = fw.to_dict()
    else:
        # Get latest version
        version_info = get_firmware_version(platform, product)
    
    if not version_info:
        return jsonify({'error': 'No firmware available'}), 404
    
    # Check if full flash is requested but bootloader/partitions are not available
    if flash_mode == 'full':
        has_bootloader = version_info.get('has_bootloader', False)
        has_partitions = version_info.get('has_partitions', False)
        
        if not has_bootloader or not has_partitions:
            return jsonify({
                'error': 'Full flash not available for this firmware',
                'message': 'Bootloader and/or partition table files are missing. Use quick flash mode instead.',
                'has_bootloader': has_bootloader,
                'has_partitions': has_partitions
            }), 400
    
    # Get chip family for this platform
    chip_family = PLATFORM_CHIP_FAMILY.get(platform, 'ESP32')
    
    # Generate user-scoped download tokens for the manifest
    # Include version in token if specific version was requested
    firmware_token = generate_user_download_token(user_id, platform, product, max_age=3600, version=requested_version)
    firmware_url = url_for('user_download_firmware_file', token=firmware_token, _external=True)
    
    # Determine correct flash offsets based on chip family
    # ESP32 family: firmware goes to 0x10000 (bootloader at 0x0, partition table at 0x8000)
    # ESP8266: firmware goes to 0x0 (different bootloader architecture)
    is_esp32_family = chip_family in ['ESP32', 'ESP32-C3', 'ESP32-S3']
    
    # Build parts list based on flash mode
    parts = []
    
    if flash_mode == 'full' and is_esp32_family:
        # Full flash: bootloader, partitions, then firmware
        bootloader_token = generate_user_download_token(user_id, f"{platform}-bootloader", product, max_age=3600, version=requested_version)
        partitions_token = generate_user_download_token(user_id, f"{platform}-partitions", product, max_age=3600, version=requested_version)
        
        bootloader_url = url_for('user_download_bootloader', token=bootloader_token, _external=True)
        partitions_url = url_for('user_download_partitions', token=partitions_token, _external=True)
        
        parts = [
            {"path": bootloader_url, "offset": 0},        # Bootloader at 0x0
            {"path": partitions_url, "offset": 0x8000},   # Partition table at 0x8000
            {"path": firmware_url, "offset": 0x10000}     # Firmware at 0x10000
        ]
    else:
        # Quick flash: firmware only
        flash_offset = 0x10000 if is_esp32_family else 0x0
        parts = [{"path": firmware_url, "offset": flash_offset}]
    
    # Create ESP Web Tools manifest
    manifest = {
        "name": f"Norrtek IoT - {platform.upper()} ({flash_mode.upper()} Flash)",
        "version": version_info['version'],
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": chip_family,
                "parts": parts
            }
        ]
    }
    
    # Return with CORS headers for ESP Web Tools
    response = jsonify(manifest)
    response.headers['Access-Control-Allow-Origin'] = '*'
    response.headers['Access-Control-Allow-Methods'] = 'GET, OPTIONS'
    response.headers['Access-Control-Allow-Headers'] = 'Content-Type'
    
    return response

@app.route('/firmware/user-download-firmware/<token>')
def user_download_firmware_file(token):
    """User-scoped firmware download endpoint using user-specific signed tokens"""
    # Verify user token and extract user_id, platform, product, and optional version
    user_id, platform, product, requested_version = verify_user_download_token(token)
    
    if not user_id or not platform or not product:
        return jsonify({'error': 'Invalid or expired download token'}), 401
    
    # Load user from database
    user = db.session.get(User, user_id)
    if not user:
        return jsonify({'error': 'User not found'}), 401
    
    # Check if user has permission to download this platform
    if not user.can_download_platform(platform):
        return jsonify({'error': f'You do not have permission to download {platform} firmware'}), 403
    
    # Validate platform
    if platform not in SUPPORTED_PLATFORMS:
        return jsonify({'error': 'Invalid platform'}), 400
    
    # Get firmware version info - either specific version or latest
    if requested_version:
        fw = FirmwareVersion.query.filter_by(product=product, platform=platform, version=requested_version).first()
        if not fw:
            return jsonify({'error': f'Version {requested_version} not found for {product}/{platform}'}), 404
        version_info = fw.to_dict()
    else:
        version_info = get_firmware_version(platform, product)
    
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
                # Use stored object_name or reconstruct with current environment prefix
                object_path = f"/{bucket_name}/{version_info.get('object_name', get_firmwares_prefix() + version_info['filename'])}"
            
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

@app.route('/firmware/user-download-bootloader/<token>')
def user_download_bootloader(token):
    """User-scoped bootloader download endpoint using user-specific signed tokens"""
    # Verify user token and extract user_id, platform (with -bootloader suffix), product, and optional version
    user_id, platform_with_suffix, product, requested_version = verify_user_download_token(token)
    
    if not user_id or not platform_with_suffix or not product:
        return jsonify({'error': 'Invalid or expired download token'}), 401
    
    # Remove -bootloader suffix to get actual platform
    platform = platform_with_suffix.replace('-bootloader', '')
    
    # Load user from database
    user = db.session.get(User, user_id)
    if not user:
        return jsonify({'error': 'User not found'}), 401
    
    # Check if user has permission to download this platform
    if not user.can_download_platform(platform):
        return jsonify({'error': f'You do not have permission to download {platform} bootloader'}), 403
    
    # Validate platform
    if platform not in SUPPORTED_PLATFORMS:
        return jsonify({'error': 'Invalid platform'}), 400
    
    # Get firmware version info - either specific version or latest
    if requested_version:
        fw = FirmwareVersion.query.filter_by(product=product, platform=platform, version=requested_version).first()
        if not fw:
            return jsonify({'error': f'Version {requested_version} not found for {product}/{platform}'}), 404
        version_info = fw.to_dict()
    else:
        version_info = get_firmware_version(platform, product)
    
    if not version_info or not version_info.get('bootloader_filename'):
        return jsonify({'error': 'Bootloader file not available'}), 404
    
    # Check where the bootloader is stored
    storage_location = version_info.get('storage', 'local')
    
    if storage_location == 'object_storage' and version_info.get('bootloader_object_path'):
        # Download from Object Storage
        try:
            storage = ObjectStorageService()
            firmware_data = storage.download_as_bytes(version_info['bootloader_object_path'])
            
            response = Response(
                firmware_data,
                mimetype='application/octet-stream',
                headers={
                    'Content-Disposition': f'attachment; filename="{version_info["bootloader_filename"]}"',
                    'Content-Length': str(len(firmware_data)),
                    'Access-Control-Allow-Origin': '*',
                    'Access-Control-Allow-Methods': 'GET, OPTIONS',
                    'Access-Control-Allow-Headers': 'Content-Type'
                }
            )
            return response
        except (ObjectStorageError, ObjectNotFoundError) as e:
            print(f"Error downloading bootloader from Object Storage: {e}")
            # Try local filesystem as fallback
            bootloader_path = FIRMWARES_DIR / version_info['bootloader_filename']
            if bootloader_path.exists():
                response = send_file(
                    bootloader_path,
                    mimetype='application/octet-stream',
                    as_attachment=True,
                    download_name=version_info['bootloader_filename']
                )
                response.headers['Access-Control-Allow-Origin'] = '*'
                return response
            return jsonify({'error': 'Bootloader file not found'}), 404
    else:
        # Serve from local filesystem
        bootloader_path = FIRMWARES_DIR / version_info['bootloader_filename']
        if not bootloader_path.exists():
            return jsonify({'error': 'Bootloader file not found'}), 404
        
        response = send_file(
            bootloader_path,
            mimetype='application/octet-stream',
            as_attachment=True,
            download_name=version_info['bootloader_filename']
        )
        response.headers['Access-Control-Allow-Origin'] = '*'
        response.headers['Access-Control-Allow-Methods'] = 'GET, OPTIONS'
        response.headers['Access-Control-Allow-Headers'] = 'Content-Type'
        return response

@app.route('/firmware/user-download-partitions/<token>')
def user_download_partitions(token):
    """User-scoped partitions download endpoint using user-specific signed tokens"""
    # Verify user token and extract user_id, platform (with -partitions suffix), product, and optional version
    user_id, platform_with_suffix, product, requested_version = verify_user_download_token(token)
    
    if not user_id or not platform_with_suffix or not product:
        return jsonify({'error': 'Invalid or expired download token'}), 401
    
    # Remove -partitions suffix to get actual platform
    platform = platform_with_suffix.replace('-partitions', '')
    
    # Load user from database
    user = db.session.get(User, user_id)
    if not user:
        return jsonify({'error': 'User not found'}), 401
    
    # Check if user has permission to download this platform
    if not user.can_download_platform(platform):
        return jsonify({'error': f'You do not have permission to download {platform} partitions'}), 403
    
    # Validate platform
    if platform not in SUPPORTED_PLATFORMS:
        return jsonify({'error': 'Invalid platform'}), 400
    
    # Get firmware version info - either specific version or latest
    if requested_version:
        fw = FirmwareVersion.query.filter_by(product=product, platform=platform, version=requested_version).first()
        if not fw:
            return jsonify({'error': f'Version {requested_version} not found for {product}/{platform}'}), 404
        version_info = fw.to_dict()
    else:
        version_info = get_firmware_version(platform, product)
    
    if not version_info or not version_info.get('partitions_filename'):
        return jsonify({'error': 'Partitions file not available'}), 404
    
    # Check where the partitions are stored
    storage_location = version_info.get('storage', 'local')
    
    if storage_location == 'object_storage' and version_info.get('partitions_object_path'):
        # Download from Object Storage
        try:
            storage = ObjectStorageService()
            firmware_data = storage.download_as_bytes(version_info['partitions_object_path'])
            
            response = Response(
                firmware_data,
                mimetype='application/octet-stream',
                headers={
                    'Content-Disposition': f'attachment; filename="{version_info["partitions_filename"]}"',
                    'Content-Length': str(len(firmware_data)),
                    'Access-Control-Allow-Origin': '*',
                    'Access-Control-Allow-Methods': 'GET, OPTIONS',
                    'Access-Control-Allow-Headers': 'Content-Type'
                }
            )
            return response
        except (ObjectStorageError, ObjectNotFoundError) as e:
            print(f"Error downloading partitions from Object Storage: {e}")
            # Try local filesystem as fallback
            partitions_path = FIRMWARES_DIR / version_info['partitions_filename']
            if partitions_path.exists():
                response = send_file(
                    partitions_path,
                    mimetype='application/octet-stream',
                    as_attachment=True,
                    download_name=version_info['partitions_filename']
                )
                response.headers['Access-Control-Allow-Origin'] = '*'
                return response
            return jsonify({'error': 'Partitions file not found'}), 404
    else:
        # Serve from local filesystem
        partitions_path = FIRMWARES_DIR / version_info['partitions_filename']
        if not partitions_path.exists():
            return jsonify({'error': 'Partitions file not found'}), 404
        
        response = send_file(
            partitions_path,
            mimetype='application/octet-stream',
            as_attachment=True,
            download_name=version_info['partitions_filename']
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
    product = request.args.get('product')
    
    if not product:
        return jsonify({'error': 'Missing required parameter: product'}), 400
    
    # Apply platform firmware mapping
    if platform in PLATFORM_FIRMWARE_MAPPING:
        actual_platform = PLATFORM_FIRMWARE_MAPPING[platform]
        print(f"Platform firmware mapping: {platform} → {actual_platform}")
        platform = actual_platform
    
    if platform not in SUPPORTED_PLATFORMS:
        return jsonify({
            'error': f'Invalid platform: {platform}',
            'supported_platforms': SUPPORTED_PLATFORMS
        }), 400
    
    version_info = get_firmware_version(platform, product)
    
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
                object_path = f"/{bucket_name}/{version_info.get('object_name', get_firmwares_prefix() + version_info['filename'])}"
            
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
    bootloader_file = request.files.get('bootloader')  # Optional
    partitions_file = request.files.get('partitions')  # Optional
    version = request.form.get('version')
    platform = request.form.get('platform', '').lower()
    product = request.form.get('product')
    description = request.form.get('description', '').strip() or None  # Optional build description
    
    if not version or not platform or not product:
        return jsonify({'error': 'Missing required fields: version, platform, and product are required'}), 400
    
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
    
    current_version_info = get_firmware_version(platform, product)
    if current_version_info:
        try:
            current_version_code = parse_version(current_version_info['version'])
            if new_version_code < current_version_code:
                return jsonify({
                    'error': 'Version downgrade not allowed',
                    'current_version': current_version_info['version'],
                    'attempted_version': version,
                    'product': product
                }), 400
            elif new_version_code == current_version_code:
                return jsonify({
                    'error': 'Version already exists',
                    'current_version': current_version_info['version'],
                    'message': 'Use a newer version number to update',
                    'product': product
                }), 409
        except (ValueError, IndexError):
            pass
    
    filename = f"firmware-{product}-{platform}-{version}.bin"
    object_name = f"{get_firmwares_prefix()}{filename}"
    
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
        'product': product,
        'version': version,
        'filename': filename,
        'size': file_size,
        'sha256': file_hash,
        'uploaded_at': datetime.utcnow().isoformat(),
        'download_url': f'/api/firmware/{platform}?product={product}',
        'storage': storage_location,
        'description': description
    }
    
    # Store full object path for Object Storage downloads (includes bucket)
    if storage_location == 'object_storage' and object_path:
        version_info['object_path'] = object_path
        version_info['object_name'] = object_name
    elif filepath:
        version_info['local_path'] = str(filepath)
    
    # Process bootloader file if provided
    if bootloader_file and bootloader_file.filename:
        if not bootloader_file.filename.endswith('.bin'):
            return jsonify({'error': 'Bootloader file must be a .bin file'}), 400
        
        bootloader_filename = f"bootloader-{product}-{platform}-{version}.bin"
        bootloader_object_name = f"{get_firmwares_prefix()}{bootloader_filename}"
        
        # Read bootloader data
        bootloader_data = bootloader_file.read()
        bootloader_size = len(bootloader_data)
        bootloader_hash = hashlib.sha256(bootloader_data).hexdigest()
        
        # Upload bootloader to storage
        try:
            if storage_location == 'object_storage':
                storage = ObjectStorageService()
                temp_path = FIRMWARES_DIR / bootloader_filename
                with open(temp_path, 'wb') as f:
                    f.write(bootloader_data)
                bootloader_object_path = storage.upload_file(temp_path, bootloader_object_name)
                temp_path.unlink()
                
                version_info['bootloader_filename'] = bootloader_filename
                version_info['bootloader_size'] = bootloader_size
                version_info['bootloader_sha256'] = bootloader_hash
                version_info['bootloader_object_path'] = bootloader_object_path
                version_info['bootloader_object_name'] = bootloader_object_name
                print(f"✓ Uploaded bootloader to Object Storage: {bootloader_object_path}")
            else:
                bootloader_filepath = FIRMWARES_DIR / bootloader_filename
                with open(bootloader_filepath, 'wb') as f:
                    f.write(bootloader_data)
                
                version_info['bootloader_filename'] = bootloader_filename
                version_info['bootloader_size'] = bootloader_size
                version_info['bootloader_sha256'] = bootloader_hash
                version_info['bootloader_local_path'] = str(bootloader_filepath)
                print(f"✓ Saved bootloader to local storage: {bootloader_filepath}")
        except Exception as e:
            print(f"⚠ Failed to upload bootloader: {e}")
    
    # Process partition table file if provided
    if partitions_file and partitions_file.filename:
        if not partitions_file.filename.endswith('.bin'):
            return jsonify({'error': 'Partitions file must be a .bin file'}), 400
        
        partitions_filename = f"partitions-{product}-{platform}-{version}.bin"
        partitions_object_name = f"{get_firmwares_prefix()}{partitions_filename}"
        
        # Read partitions data
        partitions_data = partitions_file.read()
        partitions_size = len(partitions_data)
        partitions_hash = hashlib.sha256(partitions_data).hexdigest()
        
        # Upload partitions to storage
        try:
            if storage_location == 'object_storage':
                storage = ObjectStorageService()
                temp_path = FIRMWARES_DIR / partitions_filename
                with open(temp_path, 'wb') as f:
                    f.write(partitions_data)
                partitions_object_path = storage.upload_file(temp_path, partitions_object_name)
                temp_path.unlink()
                
                version_info['partitions_filename'] = partitions_filename
                version_info['partitions_size'] = partitions_size
                version_info['partitions_sha256'] = partitions_hash
                version_info['partitions_object_path'] = partitions_object_path
                version_info['partitions_object_name'] = partitions_object_name
                print(f"✓ Uploaded partitions to Object Storage: {partitions_object_path}")
            else:
                partitions_filepath = FIRMWARES_DIR / partitions_filename
                with open(partitions_filepath, 'wb') as f:
                    f.write(partitions_data)
                
                version_info['partitions_filename'] = partitions_filename
                version_info['partitions_size'] = partitions_size
                version_info['partitions_sha256'] = partitions_hash
                version_info['partitions_local_path'] = str(partitions_filepath)
                print(f"✓ Saved partitions to local storage: {partitions_filepath}")
        except Exception as e:
            print(f"⚠ Failed to upload partitions: {e}")
    
    # Update in-memory cache (keyed by product, platform)
    LATEST_VERSIONS[_cache_key(product, platform)] = version_info
    
    # Save to database
    save_version_to_db(platform, version_info, product)
    
    # Mirror esp12f firmware to legacy esp8266 alias for monitoring visibility
    if platform == 'esp12f':
        esp8266_info = version_info.copy()
        esp8266_info['download_url'] = f'/api/firmware/esp8266?product={product}'
        LATEST_VERSIONS[_cache_key(product, 'esp8266')] = esp8266_info
        # Note: Don't save alias to DB as it shares the same firmware, just cache it
    
    return jsonify({
        'success': True,
        'message': f'Firmware uploaded successfully for {product}',
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


# ============================================================================
# DEV ENVIRONMENT PROXY ENDPOINTS
# These endpoints allow production to proxy firmware uploads to dev environments
# ============================================================================

@app.route('/api/dev/register', methods=['POST'])
def register_dev_environment():
    """Register a development environment's URL with production
    
    Called by dev environment on startup to register its current URL.
    Production uses this to proxy GitHub Actions uploads to dev.
    """
    api_key = request.headers.get('X-API-Key')
    
    if api_key != UPLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    try:
        data = request.get_json()
        
        if not data or 'base_url' not in data:
            return jsonify({'error': 'Missing base_url'}), 400
        
        base_url = data['base_url'].rstrip('/')
        auth_token = data.get('auth_token', UPLOAD_API_KEY)  # Default to same API key
        name = data.get('name', 'default')
        
        # Upsert dev environment
        dev_env = DevEnvironment.query.filter_by(name=name).first()
        if dev_env:
            dev_env.base_url = base_url
            dev_env.auth_token = auth_token
            dev_env.registered_at = datetime.now(timezone.utc)
            dev_env.last_heartbeat = datetime.now(timezone.utc)
            dev_env.is_active = True
            print(f"🔄 Updated dev environment '{name}': {base_url}")
        else:
            dev_env = DevEnvironment(
                name=name,
                base_url=base_url,
                auth_token=auth_token
            )
            db.session.add(dev_env)
            print(f"✨ Registered new dev environment '{name}': {base_url}")
        
        db.session.commit()
        
        return jsonify({
            'success': True,
            'message': f'Dev environment registered: {base_url}',
            'dev_environment': dev_env.to_dict()
        }), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'error': f'Failed to register dev environment: {str(e)}'}), 500


@app.route('/api/dev/heartbeat', methods=['POST'])
def dev_environment_heartbeat():
    """Update dev environment heartbeat to keep registration active"""
    api_key = request.headers.get('X-API-Key')
    
    if api_key != UPLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    try:
        data = request.get_json() or {}
        name = data.get('name', 'default')
        
        dev_env = DevEnvironment.query.filter_by(name=name).first()
        if not dev_env:
            return jsonify({'error': f'Dev environment not registered: {name}'}), 404
        
        dev_env.last_heartbeat = datetime.now(timezone.utc)
        db.session.commit()
        
        return jsonify({
            'success': True,
            'dev_environment': dev_env.to_dict()
        }), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'error': f'Failed to update heartbeat: {str(e)}'}), 500


@app.route('/api/dev/status')
def dev_environment_status():
    """Get status of registered dev environments (requires auth)"""
    api_key = request.headers.get('X-API-Key')
    
    if api_key != UPLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    dev_envs = DevEnvironment.query.filter_by(is_active=True).all()
    
    return jsonify({
        'dev_environments': [env.to_dict() for env in dev_envs],
        'count': len(dev_envs)
    })


@app.route('/api/dev/upload', methods=['POST'])
def proxy_upload_to_dev():
    """Proxy firmware upload to registered dev environment
    
    GitHub Actions calls this endpoint on production, which forwards
    the upload to the registered dev environment's /api/upload endpoint.
    """
    api_key = request.headers.get('X-API-Key')
    
    if api_key != UPLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    # Get the dev environment to proxy to
    name = request.args.get('env', 'default')
    dev_env = DevEnvironment.query.filter_by(name=name, is_active=True).first()
    
    if not dev_env:
        return jsonify({'error': f'No active dev environment registered: {name}'}), 404
    
    # Check if dev environment is healthy (heartbeat within 10 minutes)
    minutes_since_heartbeat = (datetime.now(timezone.utc) - dev_env.last_heartbeat).total_seconds() / 60
    if minutes_since_heartbeat > 10:
        return jsonify({
            'error': f'Dev environment is stale (last heartbeat {minutes_since_heartbeat:.1f} minutes ago)',
            'suggestion': 'Start the dev environment and wait for it to re-register'
        }), 503
    
    try:
        # Forward the multipart form data to the dev environment
        target_url = f"{dev_env.base_url}/api/upload"
        
        # Rebuild the files and form data from the incoming request
        files = {}
        for key in request.files:
            file = request.files[key]
            files[key] = (file.filename, file.read(), file.content_type)
        
        form_data = {}
        for key in request.form:
            form_data[key] = request.form[key]
        
        # Forward to dev environment
        print(f"📤 Proxying upload to dev: {target_url}")
        response = requests.post(
            target_url,
            headers={'X-API-Key': dev_env.auth_token},
            files=files,
            data=form_data,
            timeout=60
        )
        
        # Return the dev environment's response
        print(f"📥 Dev response: {response.status_code}")
        return Response(
            response.content,
            status=response.status_code,
            content_type=response.headers.get('Content-Type', 'application/json')
        )
    except requests.exceptions.Timeout:
        return jsonify({'error': 'Dev environment timed out'}), 504
    except requests.exceptions.ConnectionError as e:
        return jsonify({
            'error': f'Cannot reach dev environment: {str(e)}',
            'dev_url': dev_env.base_url
        }), 502
    except Exception as e:
        return jsonify({'error': f'Proxy error: {str(e)}'}), 500


@app.route('/api/dev/config', methods=['POST'])
def proxy_config_to_dev():
    """Proxy config upload to registered dev environment"""
    api_key = request.headers.get('X-API-Key')
    
    if api_key != UPLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    name = request.args.get('env', 'default')
    dev_env = DevEnvironment.query.filter_by(name=name, is_active=True).first()
    
    if not dev_env:
        return jsonify({'error': f'No active dev environment registered: {name}'}), 404
    
    try:
        target_url = f"{dev_env.base_url}/api/config"
        
        response = requests.post(
            target_url,
            headers={
                'X-API-Key': dev_env.auth_token,
                'Content-Type': 'application/json'
            },
            json=request.get_json(),
            timeout=30
        )
        
        return Response(
            response.content,
            status=response.status_code,
            content_type=response.headers.get('Content-Type', 'application/json')
        )
    except Exception as e:
        return jsonify({'error': f'Proxy error: {str(e)}'}), 500


@app.route('/api/firmware-metadata')
def firmware_metadata():
    """Public API endpoint for firmware metadata (used by dev environments for sync)"""
    api_key = request.headers.get('X-API-Key')
    
    if api_key != DOWNLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    # Convert cache to serializable format: {product: {platform: version_info}}
    firmwares = {}
    for (product, platform), version_info in LATEST_VERSIONS.items():
        if product not in firmwares:
            firmwares[product] = {}
        firmwares[product][platform] = version_info
    
    return jsonify({
        'firmwares': firmwares,
        'timestamp': datetime.now(timezone.utc).isoformat()
    })

@app.route('/api/status')
@login_required
def status():
    # Get user from session
    user_id = session.get('user_id')
    user = db.session.get(User, user_id)
    
    if not user:
        return jsonify({'error': 'User not found'}), 401
    
    # Generate fresh user-scoped download tokens ONLY for platforms the user has permission for
    # Structure: {product: {platform: version_info}}
    firmwares_with_tokens = {}
    for (product, platform), fw_data in LATEST_VERSIONS.items():
        if product not in firmwares_with_tokens:
            firmwares_with_tokens[product] = {}
            
        if fw_data:
            # Check if user has permission to download this platform
            if user.can_download_platform(platform):
                fw_copy = fw_data.copy()
                # Generate fresh user-scoped token valid for 1 hour
                token = generate_user_download_token(user_id, platform, product, max_age=3600)
                fw_copy['public_download_url'] = f'/firmware/user-download-firmware/{token}'
                fw_copy['can_download'] = True
                firmwares_with_tokens[product][platform] = fw_copy
            else:
                # Include firmware metadata but no download token
                fw_copy = fw_data.copy()
                fw_copy['can_download'] = False
                fw_copy['public_download_url'] = None
                firmwares_with_tokens[product][platform] = fw_copy
        else:
            firmwares_with_tokens[product][platform] = None
    
    return jsonify({
        'firmwares': firmwares_with_tokens,
        'storage': {
            'files': [f.name for f in FIRMWARES_DIR.glob('*.bin')],
            'count': len(list(FIRMWARES_DIR.glob('*.bin')))
        },
        'config_source': 'file' if CONFIG_FILE.exists() else 'environment'
    })


@app.route('/api/environment')
@login_required
def get_environment():
    """Get current environment information (dev vs production)"""
    is_prod = is_production_environment()
    dev_domain = os.getenv('REPLIT_DEV_DOMAIN', '')
    
    return jsonify({
        'environment': 'production' if is_prod else 'development',
        'is_production': is_prod,
        'dev_domain': dev_domain if not is_prod else None,
        'replit_deployment': os.getenv('REPLIT_DEPLOYMENT', 'not_set')
    })


@app.route('/api/devices')
@login_required
def devices():
    """Get all active devices with online/offline status (15 minute threshold)
    
    Excludes soft-deleted devices (those with deleted_at set)
    """
    all_devices = Device.query.filter(Device.deleted_at.is_(None)).all()
    device_list = [device.to_dict(online_threshold_minutes=15) for device in all_devices]
    
    # Sort: online devices alphabetically by device_id, offline devices by last_seen (most recent first)
    online_devices = sorted([d for d in device_list if d['online']], key=lambda d: d['device_id'].lower())
    offline_devices = sorted([d for d in device_list if not d['online']], key=lambda d: d['last_seen'], reverse=True)
    device_list = online_devices + offline_devices
    
    online_count = len(online_devices)
    
    return jsonify({
        'devices': device_list,
        'count': len(device_list),
        'online_count': online_count,
        'offline_count': len(device_list) - online_count,
        'mqtt_enabled': mqtt_client is not None,
        'environment': get_dashboard_environment_scope()
    })

@app.route('/api/devices/<device_id>', methods=['DELETE'])
@login_required
def delete_device(device_id):
    """Soft-delete a device (sets deleted_at timestamp instead of actually removing)
    
    This is much faster than hard delete as it doesn't cascade through
    thousands of related events, heartbeats, snapshots, etc.
    The device and its history remain in the database but are hidden from views.
    """
    from datetime import datetime, timezone
    
    device = Device.query.filter_by(device_id=device_id).first()
    
    if not device:
        return jsonify({'error': 'Device not found'}), 404
    
    if device.deleted_at:
        return jsonify({'error': 'Device already deleted'}), 400
    
    device.deleted_at = datetime.now(timezone.utc)
    db.session.commit()
    
    print(f"🗑️  Device soft-deleted: {device_id} ({device.board_type})")
    
    return jsonify({
        'success': True,
        'message': f'Device {device_id} removed successfully'
    }), 200

@app.route('/api/devices/<device_id>/rollback', methods=['POST'])
@login_required
def rollback_device(device_id):
    """Send rollback command to a device via MQTT"""
    from mqtt_subscriber import publish_command
    
    device = Device.query.filter_by(device_id=device_id).filter(Device.deleted_at.is_(None)).first()
    if not device:
        return jsonify({
            'success': False,
            'error': 'Device not found'
        }), 404
    
    data = request.get_json(silent=True) or {}
    target_version = data.get('target_version')
    
    success = publish_command(
        device_id=device_id,
        command='rollback',
        target_version=target_version
    )
    
    if success:
        return jsonify({
            'success': True,
            'message': f'Rollback command sent to {device_id}'
        }), 200
    else:
        return jsonify({
            'success': False,
            'error': 'Failed to send rollback command (MQTT not connected)'
        }), 503

@app.route('/api/devices/<device_id>/restart', methods=['POST'])
@login_required
def restart_device(device_id):
    """Send restart command to a device via MQTT"""
    from mqtt_subscriber import publish_command
    
    device = Device.query.filter_by(device_id=device_id).filter(Device.deleted_at.is_(None)).first()
    if not device:
        return jsonify({
            'success': False,
            'error': 'Device not found'
        }), 404
    
    success = publish_command(
        device_id=device_id,
        command='restart'
    )
    
    if success:
        return jsonify({
            'success': True,
            'message': f'Restart command sent to {device_id}'
        }), 200
    else:
        return jsonify({
            'success': False,
            'error': 'Failed to send restart command (MQTT not connected)'
        }), 503

@app.route('/api/devices/<device_id>/snapshot', methods=['POST'])
@login_required
def request_snapshot(device_id):
    """Send snapshot command to a device via MQTT"""
    from mqtt_subscriber import publish_command
    
    device = Device.query.filter_by(device_id=device_id).filter(Device.deleted_at.is_(None)).first()
    if not device:
        return jsonify({
            'success': False,
            'error': 'Device not found'
        }), 404
    
    success = publish_command(
        device_id=device_id,
        command='snapshot'
    )
    
    if success:
        return jsonify({
            'success': True,
            'message': f'Snapshot command sent to {device_id}'
        }), 200
    else:
        return jsonify({
            'success': False,
            'error': 'Failed to send snapshot command (MQTT not connected)'
        }), 503

@app.route('/api/devices/<device_id>/config', methods=['POST'])
@login_required
def set_device_config(device_id):
    """Send configuration values to a device via MQTT
    
    JSON body can include:
    - temp_offset: Temperature calibration offset in degrees C (-20.0 to 20.0)
    """
    from mqtt_subscriber import publish_config
    
    device = Device.query.filter_by(device_id=device_id).filter(Device.deleted_at.is_(None)).first()
    if not device:
        return jsonify({
            'success': False,
            'error': 'Device not found'
        }), 404
    
    data = request.get_json(silent=True) or {}
    
    if not data:
        return jsonify({
            'success': False,
            'error': 'No configuration values provided'
        }), 400
    
    config_values = {}
    
    if 'temp_offset' in data:
        try:
            temp_offset = float(data['temp_offset'])
            if temp_offset < -20.0 or temp_offset > 20.0:
                return jsonify({
                    'success': False,
                    'error': 'temp_offset must be between -20.0 and 20.0'
                }), 400
            config_values['temp_offset'] = temp_offset
        except (ValueError, TypeError):
            return jsonify({
                'success': False,
                'error': 'temp_offset must be a number'
            }), 400
    
    if not config_values:
        return jsonify({
            'success': False,
            'error': 'No valid configuration values provided'
        }), 400
    
    success = publish_config(
        device_id=device_id,
        **config_values
    )
    
    if success:
        return jsonify({
            'success': True,
            'message': f'Configuration sent to {device_id}',
            'config': config_values
        }), 200
    else:
        return jsonify({
            'success': False,
            'error': 'Failed to send configuration (MQTT not connected)'
        }), 503

@app.route('/api/devices/<device_id>/snapshots')
@login_required
def get_snapshot_history(device_id):
    """Get display snapshot history for a device"""
    from models import DisplaySnapshot
    from sqlalchemy import desc
    
    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({'error': 'Device not found'}), 404
    
    # Get query parameters
    limit = request.args.get('limit', type=int, default=50)
    offset = request.args.get('offset', type=int, default=0)
    
    # Query snapshots for this device
    snapshots = DisplaySnapshot.query.filter_by(
        device_id=device_id
    ).order_by(desc(DisplaySnapshot.timestamp)).limit(limit).offset(offset).all()
    
    # Get total count
    total_count = DisplaySnapshot.query.filter_by(device_id=device_id).count()
    
    return jsonify({
        'snapshots': [s.to_dict() for s in snapshots],
        'total_count': total_count,
        'limit': limit,
        'offset': offset
    }), 200

@app.route('/api/snapshots')
@login_required
def get_all_snapshots():
    """Get display snapshots across all devices with optional filters
    
    Query parameters:
    - device_id: Filter by specific device
    - start_date: ISO 8601 date (snapshots after this time)
    - end_date: ISO 8601 date (snapshots before this time)
    - limit: Number of snapshots to return (default: 50)
    - offset: Pagination offset (default: 0)
    """
    from models import DisplaySnapshot
    from sqlalchemy import desc
    from datetime import datetime
    
    device_id = request.args.get('device_id')
    start_date = request.args.get('start_date')
    end_date = request.args.get('end_date')
    limit = request.args.get('limit', type=int, default=50)
    offset = request.args.get('offset', type=int, default=0)
    
    # Join with Device to filter out snapshots for soft-deleted devices
    query = DisplaySnapshot.query.join(Device).filter(Device.deleted_at.is_(None))
    
    if device_id:
        query = query.filter(DisplaySnapshot.device_id == device_id)
    
    if start_date:
        try:
            start_dt = datetime.fromisoformat(start_date.replace('Z', '+00:00'))
            query = query.filter(DisplaySnapshot.timestamp >= start_dt)
        except (ValueError, AttributeError):
            return jsonify({'error': 'Invalid start_date format. Use ISO 8601.'}), 400
    
    if end_date:
        try:
            end_dt = datetime.fromisoformat(end_date.replace('Z', '+00:00'))
            query = query.filter(DisplaySnapshot.timestamp <= end_dt)
        except (ValueError, AttributeError):
            return jsonify({'error': 'Invalid end_date format. Use ISO 8601.'}), 400
    
    total = query.count()
    snapshots = query.order_by(desc(DisplaySnapshot.timestamp)).limit(limit).offset(offset).all()
    
    return jsonify({
        'snapshots': [s.to_dict() for s in snapshots],
        'total': total,
        'limit': limit,
        'offset': offset
    }), 200

@app.route('/api/firmware-history')
@login_required
def get_firmware_history():
    """Get all firmware versions grouped by product and platform (for version pinning UI)
    
    Returns all historical versions per product/platform, ordered by upload date descending.
    """
    from sqlalchemy import desc
    
    # Get all firmware versions ordered by product, platform and upload date
    all_versions = FirmwareVersion.query.order_by(
        FirmwareVersion.product,
        FirmwareVersion.platform,
        desc(FirmwareVersion.uploaded_at)
    ).all()
    
    # Group by product then platform: {product: {platform: [versions]}}
    products = {}
    for fw in all_versions:
        if fw.product not in products:
            products[fw.product] = {}
        if fw.platform not in products[fw.product]:
            products[fw.product][fw.platform] = []
        
        version_info = fw.to_dict()
        version_info['id'] = fw.id
        version_info['is_latest'] = len(products[fw.product][fw.platform]) == 0  # First one is latest
        products[fw.product][fw.platform].append(version_info)
    
    return jsonify({
        'products': products,
        'total_versions': len(all_versions)
    }), 200

@app.route('/api/firmware/<int:firmware_id>/description', methods=['PUT'])
@login_required
def update_firmware_description(firmware_id):
    """Update the description/changelog for a firmware version
    
    Body: {"description": "New description text"}
    """
    firmware = FirmwareVersion.query.get(firmware_id)
    
    if not firmware:
        return jsonify({'error': 'Firmware version not found'}), 404
    
    data = request.get_json()
    if data is None:
        return jsonify({'error': 'Request body required'}), 400
    
    description = data.get('description', '').strip()
    
    if len(description) > 500:
        return jsonify({'error': 'Description must be 500 characters or less'}), 400
    
    old_description = firmware.description
    firmware.description = description if description else None
    db.session.commit()
    
    print(f"✏️ Updated firmware description: {firmware.product}/{firmware.platform} v{firmware.version}")
    if old_description:
        print(f"   Old: {old_description[:50]}...")
    if description:
        print(f"   New: {description[:50]}...")
    
    return jsonify({
        'success': True,
        'firmware_id': firmware_id,
        'product': firmware.product,
        'platform': firmware.platform,
        'version': firmware.version,
        'description': firmware.description,
        'message': f'Description updated for {firmware.product}/{firmware.platform} v{firmware.version}'
    }), 200

@app.route('/api/devices/<device_id>/pin-version', methods=['POST', 'DELETE'])
@login_required
def pin_device_version(device_id):
    """Pin or unpin a firmware version for a device
    
    POST: Pin device to a specific version
    - Body: {"version": "2025.11.26.80"} to pin, {"version": null} to unpin
    
    DELETE: Unpin device (follow latest)
    """
    device = Device.query.filter_by(device_id=device_id).filter(Device.deleted_at.is_(None)).first()
    
    if not device:
        return jsonify({'error': 'Device not found'}), 404
    
    if request.method == 'DELETE':
        # Unpin - device will follow latest version
        device.pinned_firmware_version = None
        db.session.commit()
        print(f"📌 Unpinned device {device_id} - will follow latest version")
        return jsonify({
            'success': True,
            'message': f'Device {device_id} unpinned - will follow latest version',
            'pinned_firmware_version': None
        }), 200
    
    # POST - pin to specific version
    data = request.get_json()
    if not data:
        return jsonify({'error': 'Request body required'}), 400
    
    version = data.get('version')
    
    if version is None:
        # Unpin via POST with null version
        device.pinned_firmware_version = None
        db.session.commit()
        print(f"📌 Unpinned device {device_id} - will follow latest version")
        return jsonify({
            'success': True,
            'message': f'Device {device_id} unpinned - will follow latest version',
            'pinned_firmware_version': None
        }), 200
    
    # Verify version exists for device's platform
    # Map board type to platform identifier (same mapping as MQTT handler)
    board_type = device.board_type or ''
    platform = BOARD_TYPE_TO_PLATFORM.get(board_type, board_type.lower().replace('-', '').replace(' ', ''))
    
    fw = FirmwareVersion.query.filter_by(platform=platform, version=version).first()
    
    if not fw:
        return jsonify({
            'error': f'Version {version} not found for platform {platform} (device board: {board_type})'
        }), 404
    
    # Pin the device
    device.pinned_firmware_version = version
    db.session.commit()
    print(f"📌 Pinned device {device_id} to version {version}")
    
    return jsonify({
        'success': True,
        'message': f'Device {device_id} pinned to version {version}',
        'pinned_firmware_version': version
    }), 200

@app.route('/api/devices/<device_id>/rename', methods=['POST'])
@login_required
def rename_device(device_id):
    """Update the display name for a device
    
    POST: Set or clear the display name
    - Body: {"display_name": "Hedvalla Ski Clock"} to set
    - Body: {"display_name": null} or {"display_name": ""} to clear
    """
    device = Device.query.filter_by(device_id=device_id).filter(Device.deleted_at.is_(None)).first()
    
    if not device:
        return jsonify({'error': 'Device not found'}), 404
    
    data = request.get_json()
    if not data:
        return jsonify({'error': 'Request body required'}), 400
    
    display_name = data.get('display_name', '').strip() if data.get('display_name') else None
    
    if display_name and len(display_name) > 64:
        return jsonify({'error': 'Display name must be 64 characters or less'}), 400
    
    old_name = device.display_name
    device.display_name = display_name if display_name else None
    db.session.commit()
    
    if display_name:
        print(f"✏️ Renamed device {device_id}: '{old_name}' -> '{display_name}'")
    else:
        print(f"✏️ Cleared display name for device {device_id}")
    
    return jsonify({
        'success': True,
        'device_id': device_id,
        'display_name': device.display_name,
        'message': f'Device renamed to "{display_name}"' if display_name else 'Device name cleared'
    }), 200

@app.route('/api/events')
@login_required
def get_events():
    """Get device event logs with optional filters
    
    Query parameters:
    - device_id: Filter by specific device
    - event_type: Filter by event type (e.g., 'temperature_read', 'boot', 'wifi_connect')
    - start_date: ISO 8601 date (events after this time)
    - end_date: ISO 8601 date (events before this time)
    - limit: Number of events to return (default: 100)
    - offset: Pagination offset (default: 0)
    
    Note: Excludes events from soft-deleted devices
    """
    from models import EventLog
    from sqlalchemy import desc
    from datetime import datetime
    
    device_id = request.args.get('device_id')
    event_type = request.args.get('event_type')
    start_date = request.args.get('start_date')
    end_date = request.args.get('end_date')
    limit = request.args.get('limit', type=int, default=100)
    offset = request.args.get('offset', type=int, default=0)
    
    # Join with Device to filter out events from deleted devices
    query = EventLog.query.join(Device).filter(Device.deleted_at.is_(None))
    
    if device_id:
        query = query.filter_by(device_id=device_id)
    if event_type:
        if event_type == '__other__':
            known_event_types = [
                'boot', 'device_info', 'heartbeat', 'low_heap_warning',
                'wifi_connect', 'wifi_disconnect', 'wifi_rssi_low', 'mqtt_connect', 'mqtt_disconnect',
                'config_updated', 'config_error', 'config_noop',
                'temperature_read', 'temperature_error', 'temp_sensor_not_found', 'temp_read_invalid', 'temp_read_crc_error',
                'rtc_initialized', 'rtc_not_found', 'rtc_lost_power', 'rtc_time_invalid', 'rtc_synced_from_ntp', 'rtc_sync_failed', 'rtc_drift_corrected', 'ntp_sync_success', 'ntp_sync_failed',
                'button_press', 'button_release',
                'display_mode_change'
            ]
            query = query.filter(~EventLog.event_type.in_(known_event_types))
        else:
            query = query.filter_by(event_type=event_type)
    
    if start_date:
        try:
            start_dt = datetime.fromisoformat(start_date.replace('Z', '+00:00'))
            query = query.filter(EventLog.timestamp >= start_dt)
        except (ValueError, AttributeError):
            return jsonify({'error': 'Invalid start_date format. Use ISO 8601.'}), 400
    
    if end_date:
        try:
            end_dt = datetime.fromisoformat(end_date.replace('Z', '+00:00'))
            query = query.filter(EventLog.timestamp <= end_dt)
        except (ValueError, AttributeError):
            return jsonify({'error': 'Invalid end_date format. Use ISO 8601.'}), 400
    
    total = query.count()
    events = query.order_by(desc(EventLog.timestamp)).limit(limit).offset(offset).all()
    
    return jsonify({
        'events': [e.to_dict() for e in events],
        'total': total,
        'limit': limit,
        'offset': offset
    }), 200

@app.route('/api/events/summary')
@login_required
def get_events_summary():
    """Get event summary/analytics for dashboard
    
    Query parameters:
    - device_id: Filter by specific device
    - hours: Time range in hours (default: 24)
    
    Note: Excludes events from soft-deleted devices
    """
    from models import EventLog
    from sqlalchemy import func
    from datetime import datetime, timedelta, timezone
    
    device_id = request.args.get('device_id')
    hours = request.args.get('hours', type=int, default=24)
    
    since = datetime.now(timezone.utc) - timedelta(hours=hours)
    
    # Join with Device to filter out events from deleted devices
    query = EventLog.query.join(Device).filter(Device.deleted_at.is_(None)).filter(EventLog.timestamp >= since)
    
    if device_id:
        query = query.filter_by(device_id=device_id)
    
    event_counts = query.with_entities(
        EventLog.event_type,
        func.count(EventLog.id).label('count')
    ).group_by(EventLog.event_type).all()
    
    return jsonify({
        'summary': {e.event_type: e.count for e in event_counts},
        'hours': hours,
        'since': since.isoformat()
    }), 200

@app.route('/api/ota-logs')
@login_required
def ota_logs():
    """Get OTA update logs with optional filters
    
    Note: Excludes logs for soft-deleted devices
    """
    from models import OTAUpdateLog
    from sqlalchemy import desc, and_, or_
    from datetime import datetime, timedelta
    
    # Get query parameters
    device_id = request.args.get('device_id')
    status = request.args.get('status')
    update_type = request.args.get('update_type')  # 'ota' or 'usb_flash'
    start_date = request.args.get('start_date')  # ISO 8601 format
    end_date = request.args.get('end_date')  # ISO 8601 format
    limit = request.args.get('limit', type=int, default=50)
    offset = request.args.get('offset', type=int, default=0)
    
    # Filter out logs for deleted devices (USB flash logs may have null device_id)
    query = OTAUpdateLog.query.outerjoin(Device).filter(
        or_(OTAUpdateLog.device_id.is_(None), Device.deleted_at.is_(None))
    )
    
    # Apply filters
    if device_id:
        query = query.filter_by(device_id=device_id)
    if status:
        query = query.filter_by(status=status)
    if update_type:
        query = query.filter_by(update_type=update_type)
    
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


@app.route('/api/ota-logs/<int:log_id>/log')
@login_required
def ota_log_content(log_id):
    """Get the log content for a specific OTA update log entry"""
    from models import OTAUpdateLog
    
    log = OTAUpdateLog.query.get(log_id)
    if not log:
        return jsonify({'error': 'Log entry not found'}), 404
    
    log_content = log.log_content or 'No log content available'
    
    # Return as plain text for easy viewing
    from flask import Response
    return Response(log_content, mimetype='text/plain')


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
    """Get OTA update statistics
    
    Note: Excludes updates for soft-deleted devices
    """
    from models import OTAUpdateLog
    from sqlalchemy import func, or_
    
    # Base query: join with Device to filter out soft-deleted devices
    # Use outerjoin to include USB flash logs (device_id is NULL)
    base_query = OTAUpdateLog.query.outerjoin(Device).filter(
        or_(OTAUpdateLog.device_id.is_(None), Device.deleted_at.is_(None))
    )
    
    # Total updates
    total_updates = base_query.count()
    
    # Success/failure counts
    success_count = base_query.filter(OTAUpdateLog.status == 'success').count()
    failed_count = base_query.filter(OTAUpdateLog.status == 'failed').count()
    in_progress_count = base_query.filter(
        OTAUpdateLog.status.in_(['started', 'downloading'])
    ).count()
    
    # Success rate
    success_rate = (success_count / total_updates * 100) if total_updates > 0 else 0
    
    # Average update duration (only for completed updates)
    completed_logs = base_query.filter(
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
    recent_updates = base_query.filter(
        OTAUpdateLog.started_at >= seven_days_ago
    ).count()
    
    # Failed devices (unique devices with failed updates in last 24 hours)
    # Only count non-deleted devices
    one_day_ago = datetime.now(timezone.utc) - timedelta(days=1)
    failed_devices = OTAUpdateLog.query.join(Device).filter(
        Device.deleted_at.is_(None),
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




@app.route('/api/commands', methods=['GET'])
@login_required
def get_commands():
    """Get command history with optional filters
    
    Note: Excludes commands for soft-deleted devices
    """
    from models import CommandLog
    from sqlalchemy import desc
    from datetime import datetime
    
    # Get query parameters
    device_id = request.args.get('device_id')
    command_type = request.args.get('command_type')
    start_date = request.args.get('start_date')
    end_date = request.args.get('end_date')
    limit = request.args.get('limit', type=int, default=50)
    offset = request.args.get('offset', type=int, default=0)
    
    # Join with Device to filter out commands for deleted devices
    query = CommandLog.query.join(Device).filter(Device.deleted_at.is_(None))
    
    # Apply filters
    if device_id:
        query = query.filter_by(device_id=device_id)
    if command_type:
        query = query.filter_by(command_type=command_type)
    
    # Date range filters
    if start_date:
        try:
            start_dt = datetime.fromisoformat(start_date.replace('Z', '+00:00'))
            query = query.filter(CommandLog.sent_at >= start_dt)
        except (ValueError, AttributeError):
            return jsonify({'error': 'Invalid start_date format. Use ISO 8601.'}), 400
    
    if end_date:
        try:
            end_dt = datetime.fromisoformat(end_date.replace('Z', '+00:00'))
            query = query.filter(CommandLog.sent_at <= end_dt)
        except (ValueError, AttributeError):
            return jsonify({'error': 'Invalid end_date format. Use ISO 8601.'}), 400
    
    # Get total count before pagination
    total_count = query.count()
    
    # Order by most recent first, apply pagination
    commands = query.order_by(desc(CommandLog.sent_at)).limit(limit).offset(offset).all()
    
    return jsonify({
        'commands': [cmd.to_dict() for cmd in commands],
        'count': len(commands),
        'total': total_count,
        'offset': offset,
        'limit': limit
    })


@app.route('/api/commands', methods=['POST'])
@login_required
def send_command():
    """Send a command to a device and log it"""
    from models import CommandLog, Device
    import json
    
    data = request.json
    if not data:
        return jsonify({'error': 'JSON body required'}), 400
    
    device_id = data.get('device_id')
    command_type = data.get('command_type')
    parameters = data.get('parameters', {})
    
    if not device_id:
        return jsonify({'error': 'device_id is required'}), 400
    
    if not command_type:
        return jsonify({'error': 'command_type is required'}), 400
    
    valid_commands = ['temp_offset', 'rollback', 'restart', 'snapshot', 'info']
    if command_type not in valid_commands:
        return jsonify({'error': f'Invalid command_type. Must be one of: {valid_commands}'}), 400
    
    # Validate device exists and is not deleted
    device = Device.query.filter_by(device_id=device_id).filter(Device.deleted_at.is_(None)).first()
    if not device:
        return jsonify({'error': f'Device {device_id} not found'}), 404
    
    # Validate parameters for temp_offset
    if command_type == 'temp_offset':
        temp_offset = parameters.get('temp_offset')
        if temp_offset is None:
            return jsonify({'error': 'temp_offset parameter is required'}), 400
        try:
            temp_offset = float(temp_offset)
            if temp_offset < -20 or temp_offset > 20:
                return jsonify({'error': 'temp_offset must be between -20 and 20'}), 400
        except (ValueError, TypeError):
            return jsonify({'error': 'temp_offset must be a number'}), 400
    
    # Send command via MQTT using this dashboard's environment-scoped topic
    try:
        from mqtt_subscriber import get_mqtt_client
        mqtt_client = get_mqtt_client()
        
        if mqtt_client is None:
            raise Exception("MQTT client not connected")
        
        # Use dashboard's environment for topic (database-level separation)
        env_scope = get_dashboard_environment_scope()
        
        if command_type == 'temp_offset':
            # Send config command with temp_offset
            topic = f"norrtek-iot/{env_scope}/config/{device_id}"
            payload = json.dumps({'temp_offset': parameters.get('temp_offset')})
            mqtt_client.publish(topic, payload, qos=1)
        elif command_type == 'rollback':
            topic = f"norrtek-iot/{env_scope}/command/{device_id}"
            mqtt_client.publish(topic, 'rollback', qos=1)
        elif command_type == 'restart':
            topic = f"norrtek-iot/{env_scope}/command/{device_id}"
            mqtt_client.publish(topic, 'restart', qos=1)
        elif command_type == 'snapshot':
            topic = f"norrtek-iot/{env_scope}/command/{device_id}"
            mqtt_client.publish(topic, 'snapshot', qos=1)
        elif command_type == 'info':
            topic = f"norrtek-iot/{env_scope}/command/{device_id}"
            mqtt_client.publish(topic, 'info', qos=1)
        
        status = 'sent'
        error_message = None
    except Exception as e:
        status = 'failed'
        error_message = str(e)
    
    # Log the command
    command_log = CommandLog(
        device_id=device_id,
        command_type=command_type,
        parameters=parameters if parameters else None,
        status=status,
        error_message=error_message
    )
    db.session.add(command_log)
    db.session.commit()
    
    if status == 'failed':
        return jsonify({'error': error_message, 'command': command_log.to_dict()}), 500
    
    return jsonify({'success': True, 'command': command_log.to_dict()}), 201


if __name__ == '__main__':
    # Development mode - debug enabled
    # Production deployments use gunicorn instead
    import os
    debug_mode = os.getenv('FLASK_ENV') == 'development'
    app.run(host='0.0.0.0', port=5000, debug=debug_mode)
