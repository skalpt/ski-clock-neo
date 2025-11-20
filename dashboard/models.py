from datetime import datetime, timezone
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    pass


db = SQLAlchemy(model_class=Base)


class FirmwareVersion(db.Model):
    __tablename__ = 'firmware_versions'
    
    id = db.Column(db.Integer, primary_key=True)
    platform = db.Column(db.String(32), unique=True, nullable=False, index=True)
    version = db.Column(db.String(32), nullable=False)
    filename = db.Column(db.String(128), nullable=False)
    size = db.Column(db.Integer, nullable=False)
    sha256 = db.Column(db.String(64), nullable=False)
    uploaded_at = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc))
    download_url = db.Column(db.String(128), nullable=False)
    storage = db.Column(db.String(32), nullable=False)
    object_path = db.Column(db.String(256))
    object_name = db.Column(db.String(128))
    local_path = db.Column(db.String(256))
    
    def __repr__(self):
        return f'<FirmwareVersion {self.platform} v{self.version}>'
    
    def to_dict(self):
        """Convert firmware version to dictionary for API responses"""
        result = {
            'version': self.version,
            'filename': self.filename,
            'size': self.size,
            'sha256': self.sha256,
            'uploaded_at': self.uploaded_at.isoformat(),
            'download_url': self.download_url,
            'storage': self.storage
        }
        
        if self.object_path:
            result['object_path'] = self.object_path
        if self.object_name:
            result['object_name'] = self.object_name
        if self.local_path:
            result['local_path'] = self.local_path
            
        return result


class Device(db.Model):
    __tablename__ = 'devices'
    
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(32), unique=True, nullable=False, index=True)
    board_type = db.Column(db.String(32), nullable=False)
    firmware_version = db.Column(db.String(32))
    first_seen = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc))
    last_seen = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc))
    last_uptime = db.Column(db.Integer, default=0)
    last_rssi = db.Column(db.Integer, default=0)
    last_free_heap = db.Column(db.Integer, default=0)
    
    def __repr__(self):
        return f'<Device {self.device_id} ({self.board_type})>'
    
    def to_dict(self, online_threshold_minutes=15):
        """Convert device to dictionary with online/offline status"""
        now = datetime.now(timezone.utc)
        minutes_since_last_seen = (now - self.last_seen).total_seconds() / 60
        is_online = minutes_since_last_seen < online_threshold_minutes
        
        return {
            'device_id': self.device_id,
            'board': self.board_type,
            'version': self.firmware_version,
            'first_seen': self.first_seen.isoformat(),
            'last_seen': self.last_seen.isoformat(),
            'uptime': self.last_uptime,
            'rssi': self.last_rssi,
            'free_heap': self.last_free_heap,
            'online': is_online,
            'minutes_since_last_seen': round(minutes_since_last_seen, 1)
        }
