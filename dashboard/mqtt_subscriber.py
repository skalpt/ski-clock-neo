import paho.mqtt.client as mqtt
import json
import os
import ssl
import threading
import uuid
import ipaddress
from datetime import datetime, timezone
from typing import Dict, Any, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from flask import Flask

# MQTT configuration from environment variables
MQTT_HOST = os.getenv('MQTT_HOST', 'localhost')
MQTT_PORT = 8883
MQTT_USERNAME = os.getenv('MQTT_USERNAME', '')
MQTT_PASSWORD = os.getenv('MQTT_PASSWORD', '')
MQTT_TOPIC_HEARTBEAT = "skiclock/heartbeat"
MQTT_TOPIC_VERSION_REQUEST = "skiclock/version/request"
MQTT_TOPIC_VERSION_RESPONSE = "skiclock/version/response"
MQTT_TOPIC_OTA_START = "skiclock/ota/start"
MQTT_TOPIC_OTA_PROGRESS = "skiclock/ota/progress"
MQTT_TOPIC_OTA_COMPLETE = "skiclock/ota/complete"

# Store Flask app instance for database access
_app_context = None

def set_app_context(app):
    """Set Flask app context for database operations"""
    global _app_context
    _app_context = app

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
        print(f"✓ Connected to MQTT broker: {MQTT_HOST}")
        client.subscribe(MQTT_TOPIC_HEARTBEAT)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_HEARTBEAT}")
        client.subscribe(MQTT_TOPIC_VERSION_REQUEST)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_VERSION_REQUEST}")
        client.subscribe(MQTT_TOPIC_OTA_START)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_OTA_START}")
        client.subscribe(MQTT_TOPIC_OTA_PROGRESS)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_OTA_PROGRESS}")
        client.subscribe(MQTT_TOPIC_OTA_COMPLETE)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_OTA_COMPLETE}")
    else:
        print(f"✗ Failed to connect to MQTT broker, return code {rc}")

def handle_heartbeat(client, payload):
    """Handle device heartbeat messages"""
    device_id = payload.get('device_id')
    
    if device_id and _app_context:
        # Save to database
        from models import Device, db
        
        with _app_context.app_context():
            device = Device.query.filter_by(device_id=device_id).first()
            
            if device:
                # Update existing device
                device.board_type = payload.get('board', device.board_type)
                device.firmware_version = payload.get('version', device.firmware_version)
                device.last_seen = datetime.now(timezone.utc)
                device.last_uptime = payload.get('uptime', 0)
                device.last_rssi = payload.get('rssi', 0)
                device.last_free_heap = payload.get('free_heap', 0)
                device.ssid = payload.get('ssid')
                # Validate IP address to prevent XSS attacks
                device.ip_address = validate_ip_address(payload.get('ip'))
            else:
                # Create new device
                device = Device(
                    device_id=device_id,
                    board_type=payload.get('board', 'Unknown'),
                    firmware_version=payload.get('version', 'Unknown'),
                    last_uptime=payload.get('uptime', 0),
                    last_rssi=payload.get('rssi', 0),
                    last_free_heap=payload.get('free_heap', 0),
                    ssid=payload.get('ssid'),
                    # Validate IP address to prevent XSS attacks
                    ip_address=validate_ip_address(payload.get('ip'))
                )
                db.session.add(device)
                print(f"✨ New device registered: {device_id} ({payload.get('board')})")
            
            db.session.commit()
        
        print(f"📡 Heartbeat from {device_id} ({payload.get('board')}): v{payload.get('version')}, uptime={payload.get('uptime')}s, RSSI={payload.get('rssi')}dBm")

def handle_version_request(client, payload):
    """Handle device version check requests"""
    device_id = payload.get('device_id')
    platform = payload.get('platform')
    current_version = payload.get('current_version')
    
    if not device_id or not platform:
        print(f"⚠ Invalid version request: missing device_id or platform")
        return
    
    # Get latest version info from app context
    if _app_context:
        with _app_context.app_context():
            # Import here to avoid circular imports
            from app import LATEST_VERSIONS
            
            version_info = LATEST_VERSIONS.get(platform)
            
            if version_info:
                latest_version = version_info.get('version', 'Unknown')
                update_available = latest_version != current_version
                
                # Generate session ID only if update is available
                session_id = None
                if update_available:
                    session_id = str(uuid.uuid4())
                
                # Publish response to device-specific topic
                response_topic = f"{MQTT_TOPIC_VERSION_RESPONSE}/{device_id}"
                response_payload = {
                    'latest_version': latest_version,
                    'current_version': current_version,
                    'update_available': update_available,
                    'platform': platform
                }
                
                if session_id:
                    response_payload['session_id'] = session_id
                
                client.publish(response_topic, json.dumps(response_payload), qos=1)
                print(f"📤 Version response sent to {device_id}: {latest_version} (update: {update_available})")
            else:
                print(f"⚠ No firmware found for platform: {platform}")

def handle_ota_start(client, payload):
    """Handle OTA update start notification from device"""
    session_id = payload.get('session_id')
    device_id = payload.get('device_id')
    platform = payload.get('platform')
    old_version = payload.get('old_version')
    new_version = payload.get('new_version')
    
    if not device_id or not platform or not new_version:
        print(f"⚠ Invalid OTA start message: missing required fields")
        return
    
    # Generate fallback session ID if not provided (backward compatibility)
    if not session_id:
        session_id = str(uuid.uuid4())
        print(f"ℹ️  Generated fallback session_id: {session_id}")
    
    if _app_context:
        from models import OTAUpdateLog, db
        
        with _app_context.app_context():
            # Create OTA log entry
            log = OTAUpdateLog(
                session_id=session_id,
                device_id=device_id,
                platform=platform,
                old_version=old_version,
                new_version=new_version,
                status='started'
            )
            db.session.add(log)
            db.session.commit()
            
            print(f"📝 OTA update started: {device_id} ({old_version} → {new_version}) [session: {session_id}]")

def handle_ota_progress(client, payload):
    """Handle OTA download progress updates from device"""
    session_id = payload.get('session_id')
    device_id = payload.get('device_id')
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
                db.session.commit()
                print(f"📊 OTA progress: {log.device_id} - {progress}%")
            else:
                print(f"⚠ No OTA log found for device: {device_id}")

def handle_ota_complete(client, payload):
    """Handle OTA update completion (success or failure) from device"""
    session_id = payload.get('session_id')
    device_id = payload.get('device_id')
    success = payload.get('success', False)
    error_message = payload.get('error_message')
    
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
                print(f"{status_emoji} OTA update {log.status}: {log.device_id} ({log.old_version} → {log.new_version})")
            else:
                print(f"⚠ No OTA log found for device: {device_id}")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        
        # Route message to appropriate handler based on topic
        if msg.topic == MQTT_TOPIC_HEARTBEAT:
            handle_heartbeat(client, payload)
        elif msg.topic == MQTT_TOPIC_VERSION_REQUEST:
            handle_version_request(client, payload)
        elif msg.topic == MQTT_TOPIC_OTA_START:
            handle_ota_start(client, payload)
        elif msg.topic == MQTT_TOPIC_OTA_PROGRESS:
            handle_ota_progress(client, payload)
        elif msg.topic == MQTT_TOPIC_OTA_COMPLETE:
            handle_ota_complete(client, payload)
    
    except json.JSONDecodeError as e:
        print(f"✗ Failed to parse MQTT message: {e}")
    except Exception as e:
        print(f"✗ Error processing MQTT message: {e}")

def on_disconnect(client, userdata, rc, properties=None):
    if rc != 0:
        print(f"⚠ Unexpected disconnect from MQTT broker, return code {rc}")
    else:
        print("✓ Disconnected from MQTT broker")

def start_mqtt_subscriber():
    if not MQTT_HOST or not MQTT_USERNAME or not MQTT_PASSWORD:
        print("⚠ MQTT credentials not configured, device monitoring disabled")
        return None
    
    # Generate unique client ID for each instance (prevents collision between dev/prod)
    # Format: SkiClockDashboard-<environment>-<unique_id>
    env = os.getenv('REPL_DEPLOYMENT_TYPE', 'dev')  # 'dev' or 'production'
    unique_suffix = str(uuid.uuid4())[:8]
    client_id = f"SkiClockDashboard-{env}-{unique_suffix}"
    
    print(f"Starting MQTT subscriber...")
    print(f"  Broker: {MQTT_HOST}:{MQTT_PORT}")
    print(f"  Username: {MQTT_USERNAME}")
    print(f"  Client ID: {client_id}")
    
    client = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    
    client.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS)
    client.tls_insecure_set(False)
    
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    
    try:
        client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
        client.loop_start()
        print("✓ MQTT subscriber started in background thread")
        return client
    except Exception as e:
        print(f"✗ Failed to start MQTT subscriber: {e}")
        return None
