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
MQTT_TOPIC_HEARTBEAT = "skiclock/heartbeat"
MQTT_TOPIC_VERSION_RESPONSE = "skiclock/version/response"
MQTT_TOPIC_OTA_START = "skiclock/ota/start"
MQTT_TOPIC_OTA_PROGRESS = "skiclock/ota/progress"
MQTT_TOPIC_OTA_COMPLETE = "skiclock/ota/complete"
MQTT_TOPIC_COMMAND = "skiclock/command"
MQTT_TOPIC_DISPLAY_SNAPSHOT = "skiclock/display/snapshot"

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
        print(f"✓ Connected to MQTT broker: {MQTT_HOST}")
        client.subscribe(MQTT_TOPIC_HEARTBEAT)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_HEARTBEAT}")
        client.subscribe(MQTT_TOPIC_OTA_START)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_OTA_START}")
        client.subscribe(MQTT_TOPIC_OTA_PROGRESS)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_OTA_PROGRESS}")
        client.subscribe(MQTT_TOPIC_OTA_COMPLETE)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_OTA_COMPLETE}")
        client.subscribe(f"{MQTT_TOPIC_DISPLAY_SNAPSHOT}/#")
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_DISPLAY_SNAPSHOT}/#")
    else:
        print(f"✗ Failed to connect to MQTT broker, return code {rc}")

def handle_heartbeat(client, payload):
    """Handle device heartbeat messages and check for firmware updates"""
    device_id = payload.get('device_id')
    board_type = payload.get('board', 'Unknown')
    current_version = payload.get('version', 'Unknown')
    
    if device_id and _app_context:
        # Save to database
        from models import Device, HeartbeatHistory, db
        
        with _app_context.app_context():
            device = Device.query.filter_by(device_id=device_id).first()
            
            if device:
                # Update existing device
                device.board_type = board_type
                device.firmware_version = current_version
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
                    board_type=board_type,
                    firmware_version=current_version,
                    last_uptime=payload.get('uptime', 0),
                    last_rssi=payload.get('rssi', 0),
                    last_free_heap=payload.get('free_heap', 0),
                    ssid=payload.get('ssid'),
                    # Validate IP address to prevent XSS attacks
                    ip_address=validate_ip_address(payload.get('ip'))
                )
                db.session.add(device)
                print(f"✨ New device registered: {device_id} ({board_type})")
            
            # Log heartbeat to history (for degraded status detection)
            heartbeat_log = HeartbeatHistory(
                device_id=device_id,
                timestamp=datetime.now(timezone.utc),
                rssi=payload.get('rssi'),
                uptime=payload.get('uptime'),
                free_heap=payload.get('free_heap')
            )
            db.session.add(heartbeat_log)
            
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
            
            if stuck_otas:
                # Normalize current version once
                normalized_current = current_version.lstrip('vV')
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
                            stuck_ota.error_message = f"Heartbeat shows version {current_version} instead of expected {stuck_ota.new_version} after 5+ minutes of inactivity (resolved via heartbeat)"
                            print(f"❌ OTA resolved via heartbeat (failed): {device_id} - version mismatch after timeout (expected {stuck_ota.new_version}, got {current_version})")
                            resolved_count += 1
                        # else: Active download still in progress, don't resolve yet
                
                if resolved_count > 0:
                    db.session.commit()
                    print(f"📋 Resolved {resolved_count} stuck OTA update(s) for {device_id}")
            
            # Check for firmware updates based on board type
            # Map board type to platform using authoritative mapping
            from app import get_firmware_version
            platform = BOARD_TYPE_TO_PLATFORM.get(board_type)
            
            if not platform:
                # Log when no matching platform mapping is found
                if board_type != 'Unknown':
                    print(f"⚠ No platform mapping for board type '{board_type}'")
            else:
                # Use get_firmware_version() which handles cache properly
                version_info = get_firmware_version(platform)
                
                if version_info:
                    latest_version = version_info.get('version', 'Unknown')
                    # Normalize versions by removing 'v' prefix for comparison
                    normalized_current = current_version.lstrip('vV')
                    normalized_latest = latest_version.lstrip('vV')
                    update_available = normalized_latest != normalized_current
                    
                    if update_available:
                        # Send version response to notify device of update
                        session_id = str(uuid.uuid4())
                        response_topic = f"{MQTT_TOPIC_VERSION_RESPONSE}/{device_id}"
                        response_payload = {
                            'latest_version': latest_version,
                            'current_version': current_version,
                            'update_available': True,
                            'platform': platform,
                            'session_id': session_id,
                            'subscriber_id': _subscriber_id  # Track which thread sent this
                        }
                        client.publish(response_topic, json.dumps(response_payload), qos=1)
                        print(f"📤 Version response sent to {device_id}: {latest_version} (update: True) [subscriber: {_subscriber_id}]")
                else:
                    # Log when platform exists in mapping but no firmware in cache
                    print(f"⚠ No firmware found for platform '{platform}' (board type: '{board_type}')")
        
        print(f"📡 Heartbeat from {device_id} ({board_type}): v{current_version}, uptime={payload.get('uptime')}s, RSSI={payload.get('rssi')}dBm")

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
                log.last_progress_at = datetime.now(timezone.utc)  # Update activity timestamp
                db.session.commit()
                print(f"📊 OTA progress: {log.device_id} - {progress}%")
            else:
                print(f"⚠ No OTA log found for device: {device_id}")

def handle_ota_complete(client, payload):
    """Handle OTA update completion (success or failure) from device"""
    session_id = payload.get('session_id')
    device_id = payload.get('device_id')
    
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
                print(f"{status_emoji} OTA update {log.status}: {log.device_id} ({log.old_version} → {log.new_version})")
            else:
                print(f"⚠ No OTA log found for device: {device_id}")

def handle_display_snapshot(client, payload):
    """Handle display snapshot messages"""
    device_id = payload.get('device_id')
    
    if device_id and _app_context:
        with _app_context.app_context():
            from models import Device, DisplaySnapshot, db
            
            device = Device.query.filter_by(device_id=device_id).first()
            if device:
                # Support both new 'mono' field and legacy 'pixels' field for backward compatibility
                mono_base64 = payload.get('mono') or payload.get('pixels', '')
                mono_color = payload.get('monoColor')  # [R, G, B, brightness] or None for legacy
                row_text = payload.get('row_text', [])
                
                try:
                    pixels_bytes = base64.b64decode(mono_base64)
                    
                    # Build snapshot data object - include both 'mono' and 'pixels' for compatibility
                    snapshot_data = {
                        'rows': payload.get('rows', 1),
                        'cols': payload.get('cols', 1),
                        'width': payload.get('width', 16),
                        'height': payload.get('height', 16),
                        'mono': mono_base64,   # New field
                        'pixels': mono_base64, # Legacy field for backward compatibility
                        'timestamp': datetime.now(timezone.utc).isoformat()
                    }
                    
                    # Add monoColor if present (new format)
                    if mono_color:
                        snapshot_data['monoColor'] = mono_color
                    
                    # Update device's current snapshot (for card flip view)
                    device.display_snapshot = snapshot_data
                    device.last_seen = datetime.now(timezone.utc)
                    
                    # Store snapshot in history table for debugging
                    bitmap_data = {
                        'mono': mono_base64,   # New field
                        'pixels': mono_base64, # Legacy field for backward compatibility
                        'width': payload.get('width', 16),
                        'height': payload.get('height', 16),
                        'rows': payload.get('rows', 1),
                        'cols': payload.get('cols', 1)
                    }
                    
                    # Add monoColor to bitmap_data if present
                    if mono_color:
                        bitmap_data['monoColor'] = mono_color
                    
                    snapshot = DisplaySnapshot(
                        device_id=device_id,
                        row_text=row_text,
                        bitmap_data=bitmap_data,
                        timestamp=datetime.now(timezone.utc)
                    )
                    db.session.add(snapshot)
                    db.session.commit()
                    
                    color_info = f", color={mono_color}" if mono_color else ""
                    print(f"📸 Display snapshot stored for {device_id} ({len(pixels_bytes)} bytes, {len(row_text)} rows{color_info})")
                except Exception as e:
                    print(f"✗ Failed to store display snapshot: {e}")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        
        # Route message to appropriate handler based on topic
        if msg.topic == MQTT_TOPIC_HEARTBEAT:
            handle_heartbeat(client, payload)
        elif msg.topic == MQTT_TOPIC_OTA_START:
            handle_ota_start(client, payload)
        elif msg.topic == MQTT_TOPIC_OTA_PROGRESS:
            handle_ota_progress(client, payload)
        elif msg.topic == MQTT_TOPIC_OTA_COMPLETE:
            handle_ota_complete(client, payload)
        elif msg.topic.startswith(MQTT_TOPIC_DISPLAY_SNAPSHOT):
            handle_display_snapshot(client, payload)
    
    except json.JSONDecodeError as e:
        print(f"✗ Failed to parse MQTT message: {e}")
    except Exception as e:
        print(f"✗ Error processing MQTT message: {e}")

def on_disconnect(client, userdata, rc, properties=None):
    if rc != 0:
        print(f"⚠ Unexpected disconnect from MQTT broker, return code {rc}")
    else:
        print("✓ Disconnected from MQTT broker")

def publish_command(device_id: str, command: str, **kwargs) -> bool:
    """
    Publish a command to a specific device via MQTT.
    
    Args:
        device_id: Target device ID
        command: Command type (e.g., 'rollback', 'restart', 'update')
        **kwargs: Additional command parameters
    
    Returns:
        True if published successfully, False otherwise
    """
    global _mqtt_client, _subscriber_id
    
    if not _mqtt_client or not _mqtt_client.is_connected():
        print(f"✗ Cannot publish command: MQTT client not connected")
        return False
    
    topic = f"{MQTT_TOPIC_COMMAND}/{device_id}"
    payload = {
        'command': command,
        'timestamp': datetime.now(timezone.utc).isoformat(),
        'subscriber_id': _subscriber_id,  # Track which thread sent this
        **kwargs
    }
    
    try:
        result = _mqtt_client.publish(topic, json.dumps(payload), qos=1)
        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            print(f"✓ Published command '{command}' to {device_id} [subscriber: {_subscriber_id}]")
            return True
        else:
            print(f"✗ Failed to publish command to {device_id}: error code {result.rc}")
            return False
    except Exception as e:
        print(f"✗ Error publishing command: {e}")
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
    
    if not MQTT_HOST or not MQTT_USERNAME or not MQTT_PASSWORD:
        print("⚠ MQTT credentials not configured, device monitoring disabled")
        return None
    
    # Generate unique subscriber ID for tracking message sources
    # Format: mqtt-<timestamp>-<random>
    import time
    timestamp = int(time.time() * 1000) % 1000000  # Last 6 digits of timestamp
    random_suffix = str(uuid.uuid4())[:4]
    _subscriber_id = f"mqtt-{timestamp}-{random_suffix}"
    
    # Generate unique client ID for each instance (prevents collision between dev/prod)
    # Format: SkiClockDashboard-<environment>-<unique_id>
    env = os.getenv('REPL_DEPLOYMENT_TYPE', 'dev')  # 'dev' or 'production'
    unique_suffix = str(uuid.uuid4())[:8]
    client_id = f"SkiClockDashboard-{env}-{unique_suffix}"
    
    # Create shutdown event for graceful termination
    _shutdown_event = threading.Event()
    
    print(f"Starting MQTT subscriber...")
    print(f"  Broker: {MQTT_HOST}:{MQTT_PORT}")
    print(f"  Username: {MQTT_USERNAME}")
    print(f"  Client ID: {client_id}")
    print(f"  Subscriber ID: {_subscriber_id}")
    
    client = mqtt.Client(client_id=client_id, protocol=mqtt.MQTTv311, clean_session=True)
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
