from flask import Flask, request, jsonify, send_file
import os
import hashlib
from datetime import datetime
from pathlib import Path
from typing import Dict, Optional, Any

app = Flask(__name__)

FIRMWARES_DIR = Path(__file__).parent / 'firmwares'
FIRMWARES_DIR.mkdir(exist_ok=True)

UPLOAD_API_KEY = os.getenv('UPLOAD_API_KEY', 'dev-key-change-in-production')
DOWNLOAD_API_KEY = os.getenv('DOWNLOAD_API_KEY', 'dev-download-key')

LATEST_VERSIONS: Dict[str, Optional[Dict[str, Any]]] = {
    'esp32': None,
    'esp8266': None
}

@app.route('/')
def index():
    return jsonify({
        'service': 'Ski Clock Firmware Server',
        'status': 'running',
        'endpoints': {
            'version': '/api/version?platform=esp32|esp8266',
            'download': '/api/firmware/<platform>',
            'upload': '/api/upload (POST)'
        }
    })

@app.route('/api/version')
def get_version():
    platform = request.args.get('platform', 'esp32').lower()
    
    if platform not in ['esp32', 'esp8266']:
        return jsonify({'error': 'Invalid platform. Use esp32 or esp8266'}), 400
    
    version_info = LATEST_VERSIONS.get(platform)
    
    if not version_info:
        return jsonify({'error': 'No firmware available for this platform'}), 404
    
    return jsonify(version_info)

@app.route('/api/firmware/<platform>')
def download_firmware(platform):
    api_key = request.headers.get('X-API-Key')
    
    if api_key != DOWNLOAD_API_KEY:
        return jsonify({'error': 'Invalid API key'}), 401
    
    platform = platform.lower()
    
    if platform not in ['esp32', 'esp8266']:
        return jsonify({'error': 'Invalid platform'}), 400
    
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
    
    if platform not in ['esp32', 'esp8266']:
        return jsonify({'error': 'Invalid platform'}), 400
    
    if not file.filename or not file.filename.endswith('.bin'):
        return jsonify({'error': 'File must be a .bin file'}), 400
    
    filename = f"firmware-{platform}-{version}.bin"
    filepath = FIRMWARES_DIR / filename
    
    file.save(filepath)
    
    file_size = filepath.stat().st_size
    
    with open(filepath, 'rb') as f:
        file_hash = hashlib.sha256(f.read()).hexdigest()
    
    LATEST_VERSIONS[platform] = {
        'version': version,
        'filename': filename,
        'size': file_size,
        'sha256': file_hash,
        'uploaded_at': datetime.utcnow().isoformat(),
        'download_url': f'/api/firmware/{platform}'
    }
    
    return jsonify({
        'success': True,
        'message': f'Firmware uploaded successfully',
        'version_info': LATEST_VERSIONS[platform]
    }), 201

@app.route('/api/status')
def status():
    return jsonify({
        'firmwares': LATEST_VERSIONS,
        'storage': {
            'files': [f.name for f in FIRMWARES_DIR.glob('*.bin')],
            'count': len(list(FIRMWARES_DIR.glob('*.bin')))
        }
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
