from datetime import datetime, timezone
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    pass


db = SQLAlchemy(model_class=Base)


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
