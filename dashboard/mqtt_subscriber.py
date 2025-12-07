import paho.mqtt.client as mqtt
import json
import os
import ssl
import threading
import uuid
import ipaddress
import base64
from datetime import datetime, timezone
from typing import Dict, Any, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from flask import Flask

# MQTT configuration from environment variables
MQTT_HOST = os.getenv('MQTT_HOST', 'localhost')
MQTT_PORT = 8883
MQTT_USERNAME = os.getenv('MQTT_USERNAME', '')
MQTT_PASSWORD = os.getenv('MQTT_PASSWORD', '')

def get_environment_scope() -> str:
    """Get the current environment scope using the same logic as app.py.
    
    Uses REPLIT_DEPLOYMENT detection (not PRODUCTION_API_URL) to match
    the rest of the dashboard's environment detection logic.
    
    Returns 'prod' for production deployments, 'dev' for development.
    """
    # Emergency bypass for troubleshooting
    if os.getenv('SAFE_CONFIG_BYPASS', '').lower() == 'true':
        return 'dev'
    
    # Explicit development mode
    if os.getenv('DEV_MODE', '').lower() == 'true':
        return 'dev'
    
    # Replit workspace (not a deployment) - REPL_ID exists but no REPLIT_DEPLOYMENT
    if os.getenv('REPL_ID') and not os.getenv('REPLIT_DEPLOYMENT'):
        return 'dev'
    
    # Known non-production deployment types
    deployment_type = os.getenv('REPLIT_DEPLOYMENT', '').lower()
    if deployment_type in {'staging', 'qa', 'dev'}:
        return 'dev'
    
    # Default to production (fail closed) for all other environments
    # This includes REPLIT_DEPLOYMENT="1" (published apps)
    return 'prod'

def build_topic(base_path: str) -> str:
    """Build an environment-scoped MQTT topic.
    Format: norrtek-iot/{env}/{path}
    Example: norrtek-iot/prod/heartbeat
    """
    env = get_environment_scope()
    return f"norrtek-iot/{env}/{base_path}"

# Base topic prefix for all MQTT messages
MQTT_TOPIC_PREFIX = "norrtek-iot"

# Environment-scoped MQTT topic builders
# Format: norrtek-iot/{env}/{path}/{device_id}
MQTT_TOPIC_HEARTBEAT = lambda: build_topic("heartbeat")
MQTT_TOPIC_INFO = lambda: build_topic("info")
MQTT_TOPIC_VERSION_RESPONSE = lambda: build_topic("version/response")
MQTT_TOPIC_OTA_START = lambda: build_topic("ota/start")
MQTT_TOPIC_OTA_PROGRESS = lambda: build_topic("ota/progress")
MQTT_TOPIC_OTA_COMPLETE = lambda: build_topic("ota/complete")
MQTT_TOPIC_COMMAND = lambda: build_topic("command")
MQTT_TOPIC_CONFIG = lambda: build_topic("config")
MQTT_TOPIC_DISPLAY_SNAPSHOT = lambda: build_topic("display/snapshot")
MQTT_TOPIC_EVENTS = lambda: build_topic("event")

# Store Flask app instance for database access
_app_context = None
# Store MQTT client for publishing from other modules
_mqtt_client = None
# Shutdown event for graceful termination
_shutdown_event = None
# Unique subscriber ID for tracking which thread is sending messages
_subscriber_id = None

# Map device board types (as reported by firmware) to platform identifiers
# This ensures all board names resolve correctly to their firmware cache keys
BOARD_TYPE_TO_PLATFORM = {
    'ESP32': 'esp32',
    'ESP32-C3': 'esp32c3',
    'ESP32-S3': 'esp32s3',
    'ESP-12F': 'esp12f',
    'ESP-01': 'esp01',
    'Wemos D1 Mini': 'd1mini',
}

def set_app_context(app):
    """Set Flask app context for database operations"""
    global _app_context
    _app_context = app

def get_mqtt_client():
    """Get MQTT client for publishing from other modules"""
    return _mqtt_client

def validate_ip_address(ip: str) -> Optional[str]:
    """
    Validate and sanitize IP address to prevent XSS attacks.
    Returns the IP address if valid (IPv4 or IPv6), None otherwise.
    Uses Python's built-in ipaddress module for robust validation.
    """
    if not ip or not isinstance(ip, str):
        return None
    
    try:
        # Attempt to parse as IPv4 or IPv6
        # This properly handles compressed IPv6 (e.g., 2001:db8::1)
        ip_obj = ipaddress.ip_address(ip.strip())
        # Return the normalized string representation
        return str(ip_obj)
    except ValueError:
        # Invalid IP address
        return None

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        env = get_environment_scope()
        other_env = 'prod' if env == 'dev' else 'dev'
        print(f"✓ Connected to MQTT broker: {MQTT_HOST} (dashboard env: {env})")
        
        # CRITICAL: Unsubscribe from the OTHER environment's topics first
        # This clears any stale persistent session subscriptions from previous runs
        # where the dashboard may have subscribed to different environment topics
        print(f"🧹 Clearing stale subscriptions from {other_env} environment...")
        client.unsubscribe(f"{MQTT_TOPIC_PREFIX}/{other_env}/heartbeat/+")
        client.unsubscribe(f"{MQTT_TOPIC_PREFIX}/{other_env}/info/+")
        client.unsubscribe(f"{MQTT_TOPIC_PREFIX}/{other_env}/ota/start/+")
        client.unsubscribe(f"{MQTT_TOPIC_PREFIX}/{other_env}/ota/progress/+")
        client.unsubscribe(f"{MQTT_TOPIC_PREFIX}/{other_env}/ota/complete/+")
        client.unsubscribe(f"{MQTT_TOPIC_PREFIX}/{other_env}/display/snapshot/#")
        client.unsubscribe(f"{MQTT_TOPIC_PREFIX}/{other_env}/event/#")
        # NOTE: Do NOT unsubscribe from broad wildcards like norrtek-iot/#
        # Those patterns match the current environment too, wiping out valid subscriptions
        print(f"✓ Cleared stale {other_env} subscriptions")
        
        # Subscribe ONLY to this dashboard's environment topics
        # Dev dashboard sees only dev devices, prod dashboard sees only prod devices
        # QoS levels:
        #   - Heartbeats: QoS 0 (fire-and-forget, only latest matters)
        #   - Everything else: QoS 1 (guaranteed delivery, queued when offline)
        client.subscribe(f"{MQTT_TOPIC_PREFIX}/{env}/heartbeat/+", qos=0)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_PREFIX}/{env}/heartbeat/+ (QoS 0)")
        client.subscribe(f"{MQTT_TOPIC_PREFIX}/{env}/info/+", qos=1)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_PREFIX}/{env}/info/+ (QoS 1)")
        client.subscribe(f"{MQTT_TOPIC_PREFIX}/{env}/ota/start/+", qos=1)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_PREFIX}/{env}/ota/start/+ (QoS 1)")
        client.subscribe(f"{MQTT_TOPIC_PREFIX}/{env}/ota/progress/+", qos=1)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_PREFIX}/{env}/ota/progress/+ (QoS 1)")
        client.subscribe(f"{MQTT_TOPIC_PREFIX}/{env}/ota/complete/+", qos=1)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_PREFIX}/{env}/ota/complete/+ (QoS 1)")
        client.subscribe(f"{MQTT_TOPIC_PREFIX}/{env}/display/snapshot/#", qos=1)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_PREFIX}/{env}/display/snapshot/# (QoS 1)")
        client.subscribe(f"{MQTT_TOPIC_PREFIX}/{env}/event/#", qos=1)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_PREFIX}/{env}/event/# (QoS 1)")
    else:
        print(f"✗ Failed to connect to MQTT broker, return code {rc}")

def handle_heartbeat(client, payload, topic):
    """Handle device heartbeat messages - updates telemetry data only
    
    Device ID is extracted from topic: norrtek-iot/{env}/heartbeat/{device_id}
    
    New slimmed-down format contains only dynamic telemetry:
    - rssi, uptime, free_heap, ssid, ip
    
    Legacy format (backwards compatible) may also include:
    - product, board, version (now handled by device_info topic)
    """
    # Extract device_id and environment from topic (format: norrtek-iot/{env}/heartbeat/{device_id})
    topic_parts = topic.split('/')
    if len(topic_parts) < 4:
        print(f"⚠ Invalid heartbeat topic format: {topic}")
        return
    
    environment = topic_parts[1]  # 'dev' or 'prod'
    device_id = topic_parts[3]
    
    # Validate environment - reject invalid values rather than defaulting
    if environment not in ('dev', 'prod'):
        print(f"⚠ Rejecting heartbeat with invalid environment in topic: {environment}")
        return
    
    # Legacy format: product, board, version in heartbeat payload
    # New format: these fields come from info topic instead
    board_type = payload.get('board')
    current_version = payload.get('version')
    product = payload.get('product')
    
    if device_id and _app_context:
        from models import Device, HeartbeatHistory, EventLog, db
        
        with _app_context.app_context():
            device = Device.query.filter_by(device_id=device_id).first()
            
            if device:
                # Auto-restore soft-deleted devices that send heartbeats
                # (device was deleted but is still running - user probably wants it back)
                if device.deleted_at:
                    print(f"🔄 Restoring soft-deleted device: {device_id}")
                    device.deleted_at = None
                
                # Update existing device telemetry
                device.last_seen = datetime.now(timezone.utc)
                device.last_uptime = payload.get('uptime', 0)
                device.last_rssi = payload.get('rssi', 0)
                device.last_free_heap = payload.get('free_heap', 0)
                device.ssid = payload.get('ssid')
                device.ip_address = validate_ip_address(payload.get('ip'))
                
                # Legacy format: update static fields if present in payload
                if board_type:
                    device.board_type = board_type
                if current_version:
                    device.firmware_version = current_version
                if product:
                    device.product = product
            else:
                # Device not found - check if we have enough info to create it
                if product and board_type:
                    # Legacy format: create device with full info from heartbeat
                    device = Device(
                        device_id=device_id,
                        product=product,
                        board_type=board_type,
                        firmware_version=current_version or 'Unknown',
                        last_uptime=payload.get('uptime', 0),
                        last_rssi=payload.get('rssi', 0),
                        last_free_heap=payload.get('free_heap', 0),
                        ssid=payload.get('ssid'),
                        ip_address=validate_ip_address(payload.get('ip'))
                    )
                    db.session.add(device)
                    db.session.flush()
                    print(f"✨ New device registered: {device_id} ({product}/{board_type}, env={environment})")
                else:
                    # New format: device must first send info message before heartbeat is processed
                    print(f"⚠ Heartbeat from unknown device {device_id} (env={environment}) - waiting for device info")
                    return
            
            # Use device's stored values for static fields if not in payload (new format)
            effective_product = product or device.product
            effective_board = board_type or device.board_type
            effective_version = current_version or device.firmware_version
            
            # Log heartbeat to history (for degraded status detection)
            heartbeat_log = HeartbeatHistory(
                device_id=device_id,
                timestamp=datetime.now(timezone.utc),
                rssi=payload.get('rssi'),
                uptime=payload.get('uptime'),
                free_heap=payload.get('free_heap')
            )
            db.session.add(heartbeat_log)
            
            # Log heartbeat as event for dashboard events feed
            heartbeat_event = EventLog(
                device_id=device_id,
                event_type='heartbeat',
                event_data={
                    'version': effective_version,
                    'rssi': payload.get('rssi'),
                    'uptime': payload.get('uptime'),
                    'free_heap': payload.get('free_heap'),
                    'ssid': payload.get('ssid')
                },
                timestamp=datetime.now(timezone.utc)
            )
            db.session.add(heartbeat_event)
            
            # Cleanup old heartbeat history (keep only last 24 hours)
            from datetime import timedelta
            cleanup_threshold = datetime.now(timezone.utc) - timedelta(hours=24)
            HeartbeatHistory.query.filter(
                HeartbeatHistory.device_id == device_id,
                HeartbeatHistory.timestamp < cleanup_threshold
            ).delete()
            
            db.session.commit()
            
            # Check for stuck OTA updates and resolve ALL of them based on version
            from models import OTAUpdateLog
            from datetime import timedelta
            stuck_otas = OTAUpdateLog.query.filter(
                OTAUpdateLog.device_id == device_id,
                OTAUpdateLog.status.in_(['started', 'downloading'])
            ).order_by(OTAUpdateLog.started_at.desc()).all()
            
            if stuck_otas and effective_version:
                # Normalize current version once
                normalized_current = effective_version.lstrip('vV')
                now = datetime.now(timezone.utc)
                # Timeout threshold: 5 minutes of inactivity
                timeout_threshold = now - timedelta(minutes=5)
                resolved_count = 0
                
                for stuck_ota in stuck_otas:
                    # Normalize target version for comparison
                    normalized_target = stuck_ota.new_version.lstrip('vV')
                    
                    if normalized_current == normalized_target:
                        # Success: device is now running the target version
                        stuck_ota.status = 'success'
                        stuck_ota.completed_at = now
                        stuck_ota.download_progress = 100
                        print(f"✅ OTA resolved via heartbeat (success): {device_id} ({stuck_ota.old_version} → {stuck_ota.new_version})")
                        resolved_count += 1
                    else:
                        # Check if OTA has timed out (>5 min with no progress or old progress)
                        ota_started_long_ago = stuck_ota.started_at < timeout_threshold
                        
                        # If last_progress_at is NULL, check started_at age
                        # If last_progress_at exists, check its age
                        has_timed_out = ota_started_long_ago and (
                            stuck_ota.last_progress_at is None or 
                            stuck_ota.last_progress_at < timeout_threshold
                        )
                        
                        if has_timed_out:
                            # Failed: device has different version AND OTA has timed out
                            stuck_ota.status = 'failed'
                            stuck_ota.completed_at = now
                            stuck_ota.error_message = f"Heartbeat shows version {effective_version} instead of expected {stuck_ota.new_version} after 5+ minutes of inactivity (resolved via heartbeat)"
                            print(f"❌ OTA resolved via heartbeat (failed): {device_id} - version mismatch after timeout (expected {stuck_ota.new_version}, got {effective_version})")
                            resolved_count += 1
                        # else: Active download still in progress, don't resolve yet
                
                if resolved_count > 0:
                    db.session.commit()
                    print(f"📋 Resolved {resolved_count} stuck OTA update(s) for {device_id}")
            
            # Check for firmware updates based on board type and product
            # Map board type to platform using authoritative mapping
            from app import get_firmware_version
            from models import FirmwareVersion
            platform = BOARD_TYPE_TO_PLATFORM.get(effective_board)
            
            if not platform:
                # Log when no matching platform mapping is found
                if effective_board and effective_board != 'Unknown':
                    print(f"⚠ No platform mapping for board type '{effective_board}'")
            else:
                # Check if device has a pinned firmware version
                pinned_version = device.pinned_firmware_version
                target_version = None
                
                if pinned_version:
                    # Device is pinned - verify the pinned version exists for this product
                    pinned_fw = FirmwareVersion.query.filter_by(
                        product=effective_product,
                        platform=platform, 
                        version=pinned_version
                    ).first()
                    
                    if pinned_fw:
                        target_version = pinned_version
                        print(f"📌 Device {device_id} is pinned to version {pinned_version}")
                    else:
                        # Pinned version doesn't exist - fall back to latest
                        print(f"⚠ Pinned version {pinned_version} not found for {effective_product}/{platform}, falling back to latest")
                        version_info = get_firmware_version(platform, effective_product)
                        if version_info:
                            target_version = version_info.get('version')
                else:
                    # Not pinned - use latest version for this product
                    version_info = get_firmware_version(platform, effective_product)
                    if version_info:
                        target_version = version_info.get('version')
                
                if target_version and effective_version:
                    # Normalize versions by removing 'v' prefix for comparison
                    normalized_current = effective_version.lstrip('vV')
                    normalized_target = target_version.lstrip('vV')
                    update_available = normalized_target != normalized_current
                    
                    if update_available:
                        # Send version response to notify device of update
                        # Use the device's environment from the topic
                        session_id = str(uuid.uuid4())
                        response_topic = f"{MQTT_TOPIC_PREFIX}/{environment}/version/response/{device_id}"
                        response_payload = {
                            'latest_version': target_version,
                            'current_version': effective_version,
                            'update_available': True,
                            'product': effective_product,
                            'platform': platform,
                            'session_id': session_id,
                            'subscriber_id': _subscriber_id,  # Track which thread sent this
                            'pinned': pinned_version is not None  # Let device know if this is a pinned version
                        }
                        client.publish(response_topic, json.dumps(response_payload), qos=1)
                        pin_status = "pinned" if pinned_version else "latest"
                        print(f"📤 Version response sent to {device_id} (env={environment}): {target_version} (update: True, {pin_status}) [subscriber: {_subscriber_id}]")
                else:
                    # Log when platform exists in mapping but no firmware in cache
                    print(f"⚠ No firmware found for {effective_product}/{platform} (board type: '{effective_board}')")
        
        print(f"📡 Heartbeat from {topic}: v{effective_version}, uptime={payload.get('uptime')}s, RSSI={payload.get('rssi')}dBm")

def handle_device_info(client, payload, topic):
    """Handle device info messages - updates static device data and capabilities
    
    Device ID is extracted from topic: norrtek-iot/{env}/info/{device_id}
    
    Payload format:
    {
        "product": "ski-clock",
        "board": "ESP32-C3",
        "version": "1.2.3",
        "environment": "prod",
        "config": {"temp_offset": -2.0},
        "supported_commands": ["temp_offset", "rollback", "restart", "snapshot", "info"]
    }
    
    Published on:
    - MQTT connect
    - After config changes
    - On 'info' command
    """
    # Extract device_id and environment from topic (format: norrtek-iot/{env}/info/{device_id})
    topic_parts = topic.split('/')
    if len(topic_parts) < 4:
        print(f"⚠ Invalid device info topic format: {topic}")
        return
    
    environment = topic_parts[1]  # 'dev' or 'prod' - authoritative source from topic
    device_id = topic_parts[3]
    
    # Validate environment - reject invalid values rather than defaulting
    if environment not in ('dev', 'prod'):
        print(f"⚠ Rejecting device info with invalid environment in topic: {environment}")
        return
    
    product = payload.get('product')
    board_type = payload.get('board')
    version = payload.get('version')
    payload_environment = payload.get('environment')  # Device's reported environment (for logging only)
    config = payload.get('config')
    supported_commands = payload.get('supported_commands')
    
    # Log if payload environment differs from topic (indicates potential firmware/topic mismatch)
    if payload_environment and payload_environment != environment:
        print(f"⚠ Device {device_id} reports env={payload_environment} but published to topic env={environment}")
    
    if not product or not board_type:
        print(f"⚠ Device info missing required fields from {device_id}")
        return
    
    if device_id and _app_context:
        from models import Device, EventLog, db
        
        with _app_context.app_context():
            device = Device.query.filter_by(device_id=device_id).first()
            now = datetime.now(timezone.utc)
            
            if device:
                # Auto-restore soft-deleted devices that send device info
                # (device was deleted but is still running - user probably wants it back)
                if device.deleted_at:
                    print(f"🔄 Restoring soft-deleted device: {device_id}")
                    device.deleted_at = None
                
                # Update existing device
                device.product = product
                device.board_type = board_type
                if version:
                    device.firmware_version = version
                device.last_info_at = now
                device.last_seen = now  # Also update last_seen
                
                # Update capabilities and config
                if supported_commands is not None:
                    device.supported_commands = supported_commands
                if config is not None:
                    device.last_config = config
                
                print(f"📋 Updated device info: {device_id} ({product}/{board_type} v{version})")
            else:
                # Create new device
                device = Device(
                    device_id=device_id,
                    product=product,
                    board_type=board_type,
                    firmware_version=version or 'Unknown',
                    last_info_at=now,
                    supported_commands=supported_commands,
                    last_config=config
                )
                db.session.add(device)
                db.session.flush()
                print(f"✨ New device registered via info: {device_id} ({product}/{board_type})")
            
            # Log device info as event
            info_event = EventLog(
                device_id=device_id,
                event_type='device_info',
                event_data={
                    'product': product,
                    'board': board_type,
                    'version': version,
                    'config': config,
                    'supported_commands': supported_commands
                },
                timestamp=now
            )
            db.session.add(info_event)
            
            db.session.commit()

def extract_device_id_from_topic(topic: str, base_topic: str) -> Optional[str]:
    """Extract device_id from topic path: base_topic/{device_id}[/...]
    
    Handles both single-segment (norrtek-iot/ota/start/device123) and 
    multi-segment topics (norrtek-iot/display/snapshot/device123/full).
    Returns only the first segment after base_topic as device_id.
    """
    if not topic.startswith(base_topic):
        return None
    # Topic format: base_topic/{device_id}[/optional/sub/path]
    suffix = topic[len(base_topic):]
    if suffix.startswith('/'):
        suffix = suffix[1:]
    if not suffix:
        return None
    # Take only the first segment as device_id (handles multi-segment topics)
    return suffix.split('/')[0]

def handle_ota_start(client, payload, topic):
    """Handle OTA update start notification from device
    
    Device ID is extracted from topic: norrtek-iot/{env}/ota/start/{device_id}
    Product is required in payload for multi-product support
    """
    # Extract environment and device_id from topic (format: norrtek-iot/{env}/ota/start/{device_id})
    topic_parts = topic.split('/')
    if len(topic_parts) < 5:
        print(f"⚠ Invalid OTA start topic format: {topic}")
        return
    
    environment = topic_parts[1]
    device_id = topic_parts[4]
    
    if not device_id:
        print(f"⚠ Invalid OTA start topic format: {topic}")
        return
    
    session_id = payload.get('session_id') or str(uuid.uuid4())
    product = payload.get('product')
    platform = payload.get('platform')
    old_version = payload.get('old_version')
    new_version = payload.get('new_version')
    
    if not product or not platform or not new_version:
        print(f"⚠ Invalid OTA start message: missing required fields (product={product}, platform={platform}, new_version={new_version})")
        return
    
    if _app_context:
        from models import OTAUpdateLog, db
        
        with _app_context.app_context():
            # Mark any existing in-progress OTA logs for this device as failed
            # This handles the case where a previous OTA was interrupted (e.g., WiFi dropped)
            interrupted_logs = OTAUpdateLog.query.filter(
                OTAUpdateLog.device_id == device_id,
                OTAUpdateLog.status.in_(['started', 'downloading'])
            ).all()
            
            if interrupted_logs:
                for interrupted_log in interrupted_logs:
                    interrupted_log.status = 'failed'
                    interrupted_log.completed_at = datetime.now(timezone.utc)
                    interrupted_log.error_message = 'Interrupted by new OTA attempt'
                    print(f"⚠️ Marked interrupted OTA as failed: {interrupted_log.session_id} (was at {interrupted_log.download_progress}%)")
                db.session.commit()
            
            # Create OTA log entry with product
            log = OTAUpdateLog(
                session_id=session_id,
                device_id=device_id,
                product=product,
                platform=platform,
                old_version=old_version,
                new_version=new_version,
                status='started'
            )
            db.session.add(log)
            db.session.commit()
            
            print(f"📝 OTA update started: {device_id} ({product}/{platform}: {old_version} → {new_version}) [session: {session_id}]")

def handle_ota_progress(client, payload, topic):
    """Handle OTA download progress updates from device
    
    Device ID is extracted from topic: norrtek-iot/{env}/ota/progress/{device_id}
    """
    # Extract environment and device_id from topic (format: norrtek-iot/{env}/ota/progress/{device_id})
    topic_parts = topic.split('/')
    if len(topic_parts) < 5:
        print(f"⚠ Invalid OTA progress topic format: {topic}")
        return
    
    environment = topic_parts[1]
    device_id = topic_parts[4]
    
    if not device_id:
        print(f"⚠ Invalid OTA progress topic format: {topic}")
        return
    
    session_id = payload.get('session_id')
    progress = payload.get('progress', 0)
    
    if _app_context:
        from models import OTAUpdateLog, db
        
        with _app_context.app_context():
            log = None
            
            # Try to find by session_id first
            if session_id:
                log = OTAUpdateLog.query.filter_by(session_id=session_id).first()
            
            # Fallback: find most recent in-progress update for this device
            if not log and device_id:
                log = OTAUpdateLog.query.filter(
                    OTAUpdateLog.device_id == device_id,
                    OTAUpdateLog.status.in_(['started', 'downloading'])
                ).order_by(OTAUpdateLog.started_at.desc()).first()
                
                if log:
                    print(f"ℹ️  Matched progress to recent OTA session for {device_id}")
            
            if log:
                log.download_progress = progress
                log.status = 'downloading'
                log.last_progress_at = datetime.now(timezone.utc)  # Update activity timestamp
                db.session.commit()
                print(f"📊 OTA progress: {log.device_id} - {progress}%")
            else:
                print(f"⚠ No OTA log found for device: {device_id}")

def handle_ota_complete(client, payload, topic):
    """Handle OTA update completion (success or failure) from device
    
    Device ID is extracted from topic: norrtek-iot/{env}/ota/complete/{device_id}
    """
    # Extract environment and device_id from topic (format: norrtek-iot/{env}/ota/complete/{device_id})
    topic_parts = topic.split('/')
    if len(topic_parts) < 5:
        print(f"⚠ Invalid OTA complete topic format: {topic}")
        return
    
    environment = topic_parts[1]
    device_id = topic_parts[4]
    
    if not device_id:
        print(f"⚠ Invalid OTA complete topic format: {topic}")
        return
    
    session_id = payload.get('session_id')
    
    # Dual-path parser: support both current format (status string) and legacy format (success boolean)
    status_str = payload.get('status')
    if status_str is not None:
        # Current firmware format: {"status": "success"} or {"status": "failed"}
        success = (status_str == 'success')
    else:
        # Legacy format fallback: {"success": true} or {"success": false}
        success_bool = payload.get('success')
        if success_bool is not None:
            success = success_bool
            print(f"ℹ️  Using legacy success boolean format for device {device_id}")
        else:
            # Neither format found - log warning and skip update to preserve existing state
            print(f"⚠ No status indicator in OTA complete message from {device_id}, payload: {payload}")
            return
    
    # Support both error field names
    error_message = payload.get('error') or payload.get('error_message')
    
    if _app_context:
        from models import OTAUpdateLog, db
        
        with _app_context.app_context():
            log = None
            
            # Try to find by session_id first
            if session_id:
                log = OTAUpdateLog.query.filter_by(session_id=session_id).first()
            
            # Fallback: find most recent in-progress update for this device
            if not log and device_id:
                log = OTAUpdateLog.query.filter(
                    OTAUpdateLog.device_id == device_id,
                    OTAUpdateLog.status.in_(['started', 'downloading'])
                ).order_by(OTAUpdateLog.started_at.desc()).first()
                
                if log:
                    print(f"ℹ️  Matched completion to recent OTA session for {device_id}")
            
            if log:
                log.status = 'success' if success else 'failed'
                log.completed_at = datetime.now(timezone.utc)
                log.download_progress = 100 if success else log.download_progress
                
                if error_message:
                    log.error_message = error_message
                
                db.session.commit()
                
                status_emoji = "✅" if success else "❌"
                error_info = f" - {error_message}" if error_message else ""
                print(f"{status_emoji} OTA update {log.status}: {log.device_id} ({log.old_version} → {log.new_version}){error_info}")
            else:
                print(f"⚠ No OTA log found for device: {device_id}")

def handle_display_snapshot(client, payload, topic):
    """Handle display snapshot messages with per-row structure or legacy format
    
    Device ID is extracted from topic: norrtek-iot/{env}/display/snapshot/{device_id}
    
    Supports two formats:
    
    1. New per-row format (variable panel counts per row):
    {
        "rows": [
            {"text": "12:34", "cols": 3, "width": 48, "height": 16, "mono": "base64...", "monoColor": [R,G,B,brightness]},
            {"text": "68°F", "cols": 4, "width": 64, "height": 16, "mono": "base64...", "monoColor": [R,G,B,brightness]}
        ]
    }
    
    2. Legacy format (uniform panel grid):
    {
        "mono": "base64...",
        "monoColor": [R,G,B,brightness],
        "width": 48,
        "height": 32,
        "rows": 2,
        "cols": 3,
        "row_text": ["12:34", "68°F"]
    }
    """
    # Extract environment and device_id from topic (format: norrtek-iot/{env}/display/snapshot/{device_id})
    topic_parts = topic.split('/')
    if len(topic_parts) < 5:
        print(f"⚠ Invalid display snapshot topic format: {topic}")
        return
    
    environment = topic_parts[1]
    device_id = topic_parts[4]
    
    if not device_id:
        print(f"⚠ Invalid display snapshot topic format: {topic}")
        return
    
    if _app_context:
        with _app_context.app_context():
            from models import Device, DisplaySnapshot, db
            
            device = Device.query.filter_by(device_id=device_id).first()
            if device:
                try:
                    # Check for new per-row format: {"rows": [{...}, {...}]}
                    rows_data = payload.get('rows')
                    
                    if rows_data and isinstance(rows_data, list) and len(rows_data) > 0 and isinstance(rows_data[0], dict):
                        # NEW PER-ROW FORMAT: Each row is a dict with own dimensions
                        total_bytes = 0
                        row_texts = []
                        for i, row in enumerate(rows_data):
                            if not isinstance(row, dict):
                                print(f"✗ Invalid row {i} in snapshot for {device_id}")
                                return
                            
                            mono_base64 = row.get('mono')
                            if not mono_base64:
                                print(f"✗ Row {i} missing 'mono' data for {device_id}")
                                return
                            
                            row_bytes = base64.b64decode(mono_base64)
                            total_bytes += len(row_bytes)
                            row_texts.append(row.get('text', ''))
                        
                        snapshot_data = {
                            'rows': rows_data,
                            'timestamp': datetime.now(timezone.utc).isoformat()
                        }
                        
                        device.display_snapshot = snapshot_data
                        device.last_seen = datetime.now(timezone.utc)
                        
                        bitmap_data = {
                            'rows': rows_data
                        }
                        
                        snapshot = DisplaySnapshot(
                            device_id=device_id,
                            row_text=row_texts,
                            bitmap_data=bitmap_data,
                            timestamp=datetime.now(timezone.utc)
                        )
                        db.session.add(snapshot)
                        db.session.commit()
                        
                        row_info = ", ".join([f"{r.get('cols', '?')}x{r.get('height', '?')}" for r in rows_data])
                        print(f"📸 Display snapshot stored for {device_id} ({total_bytes} bytes, rows: [{row_info}])")
                    
                    else:
                        # LEGACY FORMAT: Single mono/color with shared dimensions
                        mono_base64 = payload.get('mono') or payload.get('pixels', '')
                        mono_color = payload.get('monoColor')
                        color_base64 = payload.get('color')
                        row_text = payload.get('row_text', [])
                        
                        if not mono_base64 and not color_base64:
                            print(f"✗ No pixel data in snapshot for {device_id}")
                            return
                        
                        if mono_base64:
                            pixels_bytes = base64.b64decode(mono_base64)
                        elif color_base64:
                            pixels_bytes = base64.b64decode(color_base64)
                        
                        snapshot_data = {
                            'rows': payload.get('rows', 1),
                            'cols': payload.get('cols', 1),
                            'width': payload.get('width', 16),
                            'height': payload.get('height', 16),
                            'mono': mono_base64,
                            'timestamp': datetime.now(timezone.utc).isoformat()
                        }
                        
                        if mono_color:
                            snapshot_data['monoColor'] = mono_color
                        if color_base64:
                            snapshot_data['color'] = color_base64
                        
                        device.display_snapshot = snapshot_data
                        device.last_seen = datetime.now(timezone.utc)
                        
                        bitmap_data = {
                            'mono': mono_base64,
                            'width': payload.get('width', 16),
                            'height': payload.get('height', 16),
                            'rows': payload.get('rows', 1),
                            'cols': payload.get('cols', 1)
                        }
                        
                        if mono_color:
                            bitmap_data['monoColor'] = mono_color
                        if color_base64:
                            bitmap_data['color'] = color_base64
                        
                        snapshot = DisplaySnapshot(
                            device_id=device_id,
                            row_text=row_text,
                            bitmap_data=bitmap_data,
                            timestamp=datetime.now(timezone.utc)
                        )
                        db.session.add(snapshot)
                        db.session.commit()
                        
                        color_info = f", color={mono_color}" if mono_color else ""
                        print(f"📸 Display snapshot stored for {device_id} ({len(pixels_bytes)} bytes, {len(row_text)} rows{color_info}) [legacy format]")
                
                except Exception as e:
                    print(f"✗ Failed to store display snapshot: {e}")

def handle_event(client, payload, topic):
    """Handle device event messages for analytics
    
    Events are queued on device when offline and flushed when MQTT connects.
    Each event includes offset_ms which is millis() - event_timestamp_ms,
    allowing us to calculate actual event time from receive time.
    
    Payload format:
    {
        "type": "temperature_read",
        "data": {"value": 5.2},
        "offset_ms": 45000
    }
    """
    # Extract environment and device_id from topic (format: norrtek-iot/{env}/event/{device_id})
    topic_parts = topic.split('/')
    if len(topic_parts) < 4:
        print(f"⚠ Invalid event topic format: {topic}")
        return
    
    environment = topic_parts[1]  # 'dev' or 'prod'
    device_id = topic_parts[3]
    
    # Validate environment - reject invalid values rather than defaulting
    if environment not in ('dev', 'prod'):
        print(f"⚠ Rejecting event with invalid environment in topic: {environment}")
        return
    
    event_type = payload.get('type')
    event_data = payload.get('data')
    offset_ms = payload.get('offset_ms', 0)
    product = payload.get('product')
    board_type = payload.get('board')
    
    if not event_type:
        print(f"⚠ Missing event type in payload from {device_id}")
        return
    
    if _app_context:
        from models import Device, EventLog, db
        from datetime import timedelta
        
        with _app_context.app_context():
            # Calculate actual event time: receive_time - offset
            receive_time = datetime.now(timezone.utc)
            event_time = receive_time - timedelta(milliseconds=offset_ms)
            
            # Ensure device exists (create if new)
            device = Device.query.filter_by(device_id=device_id).first()
            if not device:
                if not product:
                    print(f"⚠ Cannot register new device via event: missing 'product' in payload from {device_id}")
                    return
                device = Device(
                    device_id=device_id,
                    product=product,
                    board_type=board_type or 'Unknown',
                    firmware_version='Unknown'
                )
                db.session.add(device)
                print(f"✨ New device registered via event: {device_id} ({product})")
            
            # Store the event
            event_log = EventLog(
                device_id=device_id,
                event_type=event_type,
                event_data=event_data,
                timestamp=event_time
            )
            db.session.add(event_log)
            db.session.commit()
            
            # Format data for logging
            data_str = f", data={event_data}" if event_data else ""
            offset_str = f" (offset: {offset_ms}ms)" if offset_ms > 0 else ""
            print(f"📊 Event logged: {device_id} [{event_type}]{data_str}{offset_str}")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        
        # Route message to appropriate handler based on topic
        # Device-specific topics use pattern: norrtek-iot/{env}/{path}/{device_id}
        # Match topics from both dev and prod environments
        topic_parts = msg.topic.split('/')
        if len(topic_parts) >= 3 and topic_parts[0] == 'norrtek-iot':
            topic_path = topic_parts[2]  # Third part is the path (e.g., 'heartbeat', 'info', 'ota')
            
            if topic_path == 'heartbeat':
                handle_heartbeat(client, payload, msg.topic)
            elif topic_path == 'info':
                handle_device_info(client, payload, msg.topic)
            elif topic_path == 'ota' and len(topic_parts) >= 4:
                ota_action = topic_parts[3]
                if ota_action == 'start':
                    handle_ota_start(client, payload, msg.topic)
                elif ota_action == 'progress':
                    handle_ota_progress(client, payload, msg.topic)
                elif ota_action == 'complete':
                    handle_ota_complete(client, payload, msg.topic)
            elif topic_path == 'display':
                handle_display_snapshot(client, payload, msg.topic)
            elif topic_path == 'event':
                handle_event(client, payload, msg.topic)
    
    except json.JSONDecodeError as e:
        print(f"✗ Failed to parse MQTT message: {e}")
    except Exception as e:
        print(f"✗ Error processing MQTT message: {e}")

def on_disconnect(client, userdata, rc, properties=None):
    if rc != 0:
        print(f"⚠ Unexpected disconnect from MQTT broker, return code {rc}")
    else:
        print("✓ Disconnected from MQTT broker")

def publish_command(device_id: str, command: str, environment: str = None, **kwargs) -> bool:
    """
    Publish a command to a specific device via MQTT.
    
    Args:
        device_id: Target device ID
        command: Command type (e.g., 'rollback', 'restart', 'update')
        environment: Optional target environment ('dev' or 'prod'). If not provided,
                     uses the dashboard's environment scope.
        **kwargs: Additional command parameters
    
    Returns:
        True if published successfully, False otherwise
    """
    global _mqtt_client, _subscriber_id, _app_context
    
    if not _mqtt_client or not _mqtt_client.is_connected():
        print(f"✗ Cannot publish command: MQTT client not connected")
        return False
    
    # Use dashboard's environment scope if not explicitly provided
    if environment is None:
        environment = get_environment_scope()
    
    topic = f"{MQTT_TOPIC_PREFIX}/{environment}/command/{device_id}"
    payload = {
        'command': command,
        'timestamp': datetime.now(timezone.utc).isoformat(),
        'subscriber_id': _subscriber_id,  # Track which thread sent this
        **kwargs
    }
    
    try:
        result = _mqtt_client.publish(topic, json.dumps(payload), qos=1)
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"✓ Published command '{command}' to {device_id} (env={environment}) [subscriber: {_subscriber_id}]")
            return True
        else:
            print(f"✗ Failed to publish command to {device_id}: error code {result.rc}")
            return False
    except Exception as e:
        print(f"✗ Error publishing command: {e}")
        return False

def publish_config(device_id: str, topic_environment: str = None, **config_values) -> bool:
    """
    Publish configuration values to a specific device via MQTT.
    
    Uses this dashboard's environment scope for the MQTT topic.
    
    Args:
        device_id: Target device ID
        topic_environment: Optional explicit topic environment override. If not provided,
                          uses the dashboard's environment scope.
        **config_values: Configuration key-value pairs (e.g., temp_offset=-2.0)
    
    Returns:
        True if published successfully, False otherwise
    """
    global _mqtt_client, _subscriber_id, _app_context
    
    if not _mqtt_client or not _mqtt_client.is_connected():
        print(f"✗ Cannot publish config: MQTT client not connected")
        return False
    
    # Use explicit topic_environment if provided, otherwise use dashboard's environment scope
    current_environment = topic_environment if topic_environment else get_environment_scope()
    
    topic = f"{MQTT_TOPIC_PREFIX}/{current_environment}/config/{device_id}"
    payload = {
        'timestamp': datetime.now(timezone.utc).isoformat(),
        'subscriber_id': _subscriber_id,
        **config_values
    }
    
    try:
        result = _mqtt_client.publish(topic, json.dumps(payload), qos=1)
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            # Log environment change details if promoting/demoting
            if 'environment' in config_values:
                print(f"✓ Published config to {device_id} (current_env={current_environment}, target_env={config_values['environment']}): {config_values} [subscriber: {_subscriber_id}]")
            else:
                print(f"✓ Published config to {device_id} (env={current_environment}): {config_values} [subscriber: {_subscriber_id}]")
            return True
        else:
            print(f"✗ Failed to publish config to {device_id}: error code {result.rc}")
            return False
    except Exception as e:
        print(f"✗ Error publishing config: {e}")
        return False

def ota_timeout_checker():
    """
    Background thread that checks for timed-out OTA updates.
    Runs every minute and fails updates that have been inactive for > 5 minutes.
    """
    global _shutdown_event, _app_context
    
    print("✓ OTA timeout checker started")
    
    while _shutdown_event and not _shutdown_event.is_set():
        if _app_context:
            try:
                from models import OTAUpdateLog, db
                from datetime import timedelta
                
                with _app_context.app_context():
                    try:
                        now = datetime.now(timezone.utc)
                        timeout_threshold = now - timedelta(minutes=5)
                        
                        # Find all in-progress OTA updates
                        in_progress_updates = OTAUpdateLog.query.filter(
                            OTAUpdateLog.status.in_(['started', 'downloading'])
                        ).all()
                        
                        timed_out_updates = []
                        
                        for ota in in_progress_updates:
                            # OTA has timed out if:
                            # 1. It was started more than 5 minutes ago AND
                            # 2. Either no progress was ever received (last_progress_at is NULL)
                            #    OR last progress was received more than 5 minutes ago
                            ota_started_long_ago = ota.started_at < timeout_threshold
                            
                            has_timed_out = ota_started_long_ago and (
                                ota.last_progress_at is None or 
                                ota.last_progress_at < timeout_threshold
                            )
                            
                            if has_timed_out:
                                timed_out_updates.append(ota)
                                
                                # Mark as failed with timeout error
                                ota.status = 'failed'
                                ota.completed_at = now
                                
                                if ota.last_progress_at:
                                    ota.error_message = f"OTA update timed out after 5 minutes of inactivity (last activity: {ota.last_progress_at.isoformat()})"
                                    print(f"⏱️ OTA update timed out: {ota.device_id} ({ota.old_version} → {ota.new_version}) - inactive since {ota.last_progress_at.isoformat()}")
                                else:
                                    ota.error_message = f"OTA update timed out after 5 minutes with no progress (started: {ota.started_at.isoformat()})"
                                    print(f"⏱️ OTA update timed out: {ota.device_id} ({ota.old_version} → {ota.new_version}) - no progress since start")
                        
                        if timed_out_updates:
                            db.session.commit()
                            print(f"⚠ Marked {len(timed_out_updates)} OTA update(s) as timed out")
                    
                    except Exception as e:
                        # Rollback on error to prevent session poisoning
                        try:
                            db.session.rollback()
                        except:
                            pass
                        print(f"✗ Error processing OTA timeouts: {e}")
                        import traceback
                        traceback.print_exc()
            
            except Exception as e:
                print(f"✗ Error in OTA timeout checker outer loop: {e}")
                import traceback
                traceback.print_exc()
        
        # Check every 60 seconds
        if _shutdown_event:
            _shutdown_event.wait(60)
    
    print("✓ OTA timeout checker stopped")

def stop_mqtt_subscriber():
    """Gracefully stop MQTT subscriber thread"""
    global _mqtt_client, _shutdown_event, _subscriber_id
    
    if _shutdown_event:
        print(f"🛑 Signaling MQTT subscriber shutdown (ID: {_subscriber_id})...")
        _shutdown_event.set()
    
    if _mqtt_client:
        try:
            _mqtt_client.loop_stop()
            _mqtt_client.disconnect()
            print(f"✓ MQTT subscriber stopped (ID: {_subscriber_id})")
        except Exception as e:
            print(f"⚠ Error stopping MQTT subscriber: {e}")
    
    _mqtt_client = None
    _shutdown_event = None
    _subscriber_id = None

def start_mqtt_subscriber():
    global _mqtt_client, _shutdown_event, _subscriber_id
    
    # Prevent duplicate subscribers (can happen with Flask reloader or workflow restarts)
    if _mqtt_client is not None:
        print(f"⚠ MQTT subscriber already running (ID: {_subscriber_id}), skipping duplicate start")
        return _mqtt_client
    
    if not MQTT_HOST or not MQTT_USERNAME or not MQTT_PASSWORD:
        print("⚠ MQTT credentials not configured, device monitoring disabled")
        return None
    
    # Generate unique subscriber ID for tracking message sources
    # Format: mqtt-<timestamp>-<random>
    import time
    timestamp = int(time.time() * 1000) % 1000000  # Last 6 digits of timestamp
    random_suffix = str(uuid.uuid4())[:4]
    _subscriber_id = f"mqtt-{timestamp}-{random_suffix}"
    
    # Use FIXED client ID for persistent sessions (allows message queuing while offline)
    # The broker will queue QoS 1/2 messages for this client while disconnected
    # Format: NorrtekDashboard-<environment> (no random suffix!)
    env = 'prod' if os.getenv('REPLIT_DEPLOYMENT') else 'dev'
    client_id = f"NorrtekDashboard-{env}"
    
    # Create shutdown event for graceful termination
    _shutdown_event = threading.Event()
    
    print(f"Starting MQTT subscriber...")
    print(f"  Broker: {MQTT_HOST}:{MQTT_PORT}")
    print(f"  Username: {MQTT_USERNAME}")
    print(f"  Client ID: {client_id}")
    print(f"  Subscriber ID: {_subscriber_id}")
    
    # Use persistent session (clean_session=False) so broker queues messages while we're offline
    # Combined with QoS 1 publishing, this ensures commands aren't lost during restarts
    client = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311, clean_session=False)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    
    client.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS)
    client.tls_insecure_set(False)
    
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    
    try:
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        client.loop_start()
        _mqtt_client = client  # Store global reference
        
        # Start OTA timeout checker thread
        timeout_thread = threading.Thread(target=ota_timeout_checker, daemon=True, name="OTATimeoutChecker")
        timeout_thread.start()
        
        print(f"✓ MQTT subscriber started (ID: {_subscriber_id})")
        return client
    except Exception as e:
        print(f"✗ Failed to start MQTT subscriber: {e}")
        _shutdown_event = None
        _subscriber_id = None
        return None
