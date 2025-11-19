import paho.mqtt.client as mqtt
import json
import os
import ssl
import threading
from datetime import datetime, timezone
from typing import Dict, Any, Optional

# MQTT configuration from environment variables
MQTT_HOST = os.getenv('MQTT_HOST', 'localhost')
MQTT_PORT = 8883
MQTT_USERNAME = os.getenv('MQTT_USERNAME', '')
MQTT_PASSWORD = os.getenv('MQTT_PASSWORD', '')
MQTT_TOPIC_HEARTBEAT = "skiclock/heartbeat"

# Device status storage
device_status: Dict[str, Dict[str, Any]] = {}
device_status_lock = threading.Lock()

def get_device_status() -> Dict[str, Dict[str, Any]]:
    with device_status_lock:
        return device_status.copy()

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"✓ Connected to MQTT broker: {MQTT_HOST}")
        client.subscribe(MQTT_TOPIC_HEARTBEAT)
        print(f"✓ Subscribed to topic: {MQTT_TOPIC_HEARTBEAT}")
    else:
        print(f"✗ Failed to connect to MQTT broker, return code {rc}")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
        device_id = payload.get('device_id')
        
        if device_id:
            with device_status_lock:
                device_status[device_id] = {
                    'device_id': device_id,
                    'board': payload.get('board', 'Unknown'),
                    'version': payload.get('version', 'Unknown'),
                    'uptime': payload.get('uptime', 0),
                    'rssi': payload.get('rssi', 0),
                    'free_heap': payload.get('free_heap', 0),
                    'last_seen': datetime.now(timezone.utc).isoformat(),
                    'raw_data': payload
                }
            
            print(f"📡 Heartbeat from {device_id} ({payload.get('board')}): v{payload.get('version')}, uptime={payload.get('uptime')}s, RSSI={payload.get('rssi')}dBm")
    
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
    
    print(f"Starting MQTT subscriber...")
    print(f"  Broker: {MQTT_HOST}:{MQTT_PORT}")
    print(f"  Username: {MQTT_USERNAME}")
    
    client = mqtt.Client(client_id="SkiClockDashboard", protocol=mqtt.MQTTv311)
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
