from datetime import datetime, timezone
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.orm import DeclarativeBase
from werkzeug.security import generate_password_hash, check_password_hash


class Base(DeclarativeBase):
    pass


db = SQLAlchemy(model_class=Base)


class Role(db.Model):
    __tablename__ = 'roles'
    
    id = db.Column(db.Integer, primary_key=True)
    name = db.Column(db.String(64), unique=True, nullable=False)
    description = db.Column(db.String(256))
    created_at = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc))
    
    # Relationships
    users = db.relationship('User', back_populates='role')
    platform_permissions = db.relationship('PlatformPermission', back_populates='role', cascade='all, delete-orphan')
    
    def __repr__(self):
        return f'<Role {self.name}>'


class PlatformPermission(db.Model):
    __tablename__ = 'platform_permissions'
    
    id = db.Column(db.Integer, primary_key=True)
    role_id = db.Column(db.Integer, db.ForeignKey('roles.id'), nullable=False)
    platform = db.Column(db.String(32), nullable=False, index=True)
    can_download = db.Column(db.Boolean, nullable=False, default=True)
    created_at = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc))
    
    # Relationships
    role = db.relationship('Role', back_populates='platform_permissions')
    
    # Unique constraint: one permission per role per platform
    __table_args__ = (
        db.UniqueConstraint('role_id', 'platform', name='uix_role_platform'),
    )
    
    def __repr__(self):
        return f'<PlatformPermission {self.role.name} -> {self.platform}>'


class User(db.Model):
    __tablename__ = 'users'
    
    id = db.Column(db.Integer, primary_key=True)
    email = db.Column(db.String(128), unique=True, nullable=False, index=True)
    password_hash = db.Column(db.String(256), nullable=False)
    role_id = db.Column(db.Integer, db.ForeignKey('roles.id'), nullable=False)
    created_at = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc))
    
    # Relationships
    role = db.relationship('Role', back_populates='users')
    download_logs = db.relationship('DownloadLog', back_populates='user', cascade='all, delete-orphan')
    
    def set_password(self, password):
        """Hash and set password"""
        self.password_hash = generate_password_hash(password)
    
    def check_password(self, password):
        """Verify password against hash"""
        return check_password_hash(self.password_hash, password)
    
    def can_download_platform(self, platform):
        """Check if user has permission to download a specific platform"""
        # Check if role has specific permission for this platform
        permission = PlatformPermission.query.filter_by(
            role_id=self.role_id,
            platform=platform
        ).first()
        
        if permission:
            return permission.can_download
        
        # Default: no explicit permission means no access (whitelist approach)
        # Can change to True for permissive default
        return False
    
    def __repr__(self):
        return f'<User {self.email}>'


class DownloadLog(db.Model):
    __tablename__ = 'download_logs'
    
    id = db.Column(db.Integer, primary_key=True)
    user_id = db.Column(db.Integer, db.ForeignKey('users.id'), nullable=False)
    platform = db.Column(db.String(32), nullable=False, index=True)
    firmware_version = db.Column(db.String(32))
    downloaded_at = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc), index=True)
    ip_address = db.Column(db.String(45))
    user_agent = db.Column(db.String(256))
    
    # Relationships
    user = db.relationship('User', back_populates='download_logs')
    
    def __repr__(self):
        return f'<DownloadLog {self.user.email} -> {self.platform} at {self.downloaded_at}>'


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
    
    # Bootloader files (optional, for full flash support)
    bootloader_filename = db.Column(db.String(128))
    bootloader_size = db.Column(db.Integer)
    bootloader_sha256 = db.Column(db.String(64))
    bootloader_object_path = db.Column(db.String(256))
    bootloader_object_name = db.Column(db.String(128))
    bootloader_local_path = db.Column(db.String(256))
    
    # Partition table files (optional, for full flash support)
    partitions_filename = db.Column(db.String(128))
    partitions_size = db.Column(db.Integer)
    partitions_sha256 = db.Column(db.String(64))
    partitions_object_path = db.Column(db.String(256))
    partitions_object_name = db.Column(db.String(128))
    partitions_local_path = db.Column(db.String(256))
    
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
            'storage': self.storage,
            'has_bootloader': bool(self.bootloader_filename),
            'has_partitions': bool(self.partitions_filename),
            'supports_full_flash': bool(self.bootloader_filename and self.partitions_filename)
        }
        
        # Firmware file paths
        if self.object_path:
            result['object_path'] = self.object_path
        if self.object_name:
            result['object_name'] = self.object_name
        if self.local_path:
            result['local_path'] = self.local_path
        
        # Bootloader metadata (top-level fields for download routes + nested for API)
        if self.bootloader_filename:
            result['bootloader_filename'] = self.bootloader_filename
            result['bootloader_size'] = self.bootloader_size
            result['bootloader_sha256'] = self.bootloader_sha256
            result['bootloader_object_path'] = self.bootloader_object_path
            result['bootloader_object_name'] = self.bootloader_object_name
            result['bootloader_local_path'] = self.bootloader_local_path
            
            # Nested object for API responses
            result['bootloader'] = {
                'filename': self.bootloader_filename,
                'size': self.bootloader_size,
                'sha256': self.bootloader_sha256
            }
        
        # Partition table metadata (top-level fields for download routes + nested for API)
        if self.partitions_filename:
            result['partitions_filename'] = self.partitions_filename
            result['partitions_size'] = self.partitions_size
            result['partitions_sha256'] = self.partitions_sha256
            result['partitions_object_path'] = self.partitions_object_path
            result['partitions_object_name'] = self.partitions_object_name
            result['partitions_local_path'] = self.partitions_local_path
            
            # Nested object for API responses
            result['partitions'] = {
                'filename': self.partitions_filename,
                'size': self.partitions_size,
                'sha256': self.partitions_sha256
            }
            
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
    ssid = db.Column(db.String(64))
    ip_address = db.Column(db.String(45))
    display_snapshot = db.Column(db.JSON)  # Stores display dimensions and base64-encoded pixel data
    
    # Relationships
    ota_update_logs = db.relationship('OTAUpdateLog', back_populates='device', cascade='all, delete-orphan')
    heartbeat_history = db.relationship('HeartbeatHistory', back_populates='device', cascade='all, delete-orphan')
    display_snapshots = db.relationship('DisplaySnapshot', back_populates='device', cascade='all, delete-orphan')
    
    def __repr__(self):
        return f'<Device {self.device_id} ({self.board_type})>'
    
    def to_dict(self, online_threshold_minutes=15):
        """Convert device to dictionary with online/offline/degraded status"""
        from datetime import timedelta
        from sqlalchemy import and_
        
        now = datetime.now(timezone.utc)
        minutes_since_last_seen = (now - self.last_seen).total_seconds() / 60
        is_online = minutes_since_last_seen < online_threshold_minutes
        
        # Calculate degraded status: 2+ consecutive missed checkins in past hour
        # Expected heartbeat interval: 60 seconds, tolerance: 90 seconds
        is_degraded = False
        if is_online:  # Only check degradation if device is online
            one_hour_ago = now - timedelta(hours=1)
            heartbeats = HeartbeatHistory.query.filter(
                and_(
                    HeartbeatHistory.device_id == self.device_id,
                    HeartbeatHistory.timestamp >= one_hour_ago
                )
            ).order_by(HeartbeatHistory.timestamp.asc()).all()
            
            if len(heartbeats) >= 3:  # Need at least 3 heartbeats to detect 2 consecutive misses
                consecutive_misses = 0
                max_consecutive_misses = 0
                
                for i in range(1, len(heartbeats)):
                    gap_seconds = (heartbeats[i].timestamp - heartbeats[i-1].timestamp).total_seconds()
                    
                    if gap_seconds > 90:  # Missed checkin (tolerance: 90 seconds)
                        consecutive_misses += 1
                        max_consecutive_misses = max(max_consecutive_misses, consecutive_misses)
                    else:
                        consecutive_misses = 0  # Reset counter on successful checkin
                
                is_degraded = max_consecutive_misses >= 2
        
        # Determine status: "online", "degraded", or "offline"
        if not is_online:
            status = "offline"
        elif is_degraded:
            status = "degraded"
        else:
            status = "online"
        
        return {
            'device_id': self.device_id,
            'board': self.board_type,
            'version': self.firmware_version,
            'first_seen': self.first_seen.isoformat(),
            'last_seen': self.last_seen.isoformat(),
            'uptime': self.last_uptime,
            'rssi': self.last_rssi,
            'free_heap': self.last_free_heap,
            'ssid': self.ssid,
            'ip_address': self.ip_address,
            'online': is_online,
            'status': status,
            'degraded': is_degraded,
            'minutes_since_last_seen': round(minutes_since_last_seen, 1),
            'display_snapshot': self.display_snapshot
        }


class OTAUpdateLog(db.Model):
    __tablename__ = 'ota_update_logs'
    
    id = db.Column(db.Integer, primary_key=True)
    session_id = db.Column(db.String(36), unique=True, nullable=True, index=True)  # UUID for tracking (nullable for backward compatibility)
    device_id = db.Column(db.String(32), db.ForeignKey('devices.device_id'), nullable=False, index=True)
    platform = db.Column(db.String(32), nullable=False, index=True)
    old_version = db.Column(db.String(32))
    new_version = db.Column(db.String(32), nullable=False)
    status = db.Column(db.String(16), nullable=False, index=True, default='started')  # 'started', 'downloading', 'success', 'failed'
    started_at = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc), index=True)
    last_progress_at = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc), index=True)  # Track last activity for timeout detection
    completed_at = db.Column(db.DateTime(timezone=True))
    error_message = db.Column(db.Text)
    download_progress = db.Column(db.Integer, default=0)  # 0-100 percentage
    
    # Relationships
    device = db.relationship('Device', back_populates='ota_update_logs')
    
    def __repr__(self):
        return f'<OTAUpdateLog {self.device_id}: {self.old_version} -> {self.new_version} ({self.status})>'
    
    def to_dict(self):
        """Convert OTA update log to dictionary"""
        result = {
            'id': self.id,
            'session_id': self.session_id,
            'device_id': self.device_id,
            'platform': self.platform,
            'old_version': self.old_version,
            'new_version': self.new_version,
            'status': self.status,
            'started_at': self.started_at.isoformat(),
            'download_progress': self.download_progress
        }
        
        if self.completed_at:
            result['completed_at'] = self.completed_at.isoformat()
            # Calculate duration in seconds
            duration = (self.completed_at - self.started_at).total_seconds()
            result['duration_seconds'] = round(duration, 1)
        
        if self.error_message:
            result['error_message'] = self.error_message
        
        return result


class HeartbeatHistory(db.Model):
    __tablename__ = 'heartbeat_history'
    
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(32), db.ForeignKey('devices.device_id'), nullable=False, index=True)
    timestamp = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc), index=True)
    rssi = db.Column(db.Integer)
    uptime = db.Column(db.Integer)
    free_heap = db.Column(db.Integer)
    
    # Relationships
    device = db.relationship('Device', back_populates='heartbeat_history')
    
    # Index for efficient time-based queries
    __table_args__ = (
        db.Index('idx_device_timestamp', 'device_id', 'timestamp'),
    )
    
    def __repr__(self):
        return f'<HeartbeatHistory {self.device_id} at {self.timestamp}>'


class DisplaySnapshot(db.Model):
    __tablename__ = 'display_snapshots'
    
    id = db.Column(db.Integer, primary_key=True)
    device_id = db.Column(db.String(32), db.ForeignKey('devices.device_id'), nullable=False, index=True)
    timestamp = db.Column(db.DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc), index=True)
    row_text = db.Column(db.JSON, nullable=False)  # JSON array: ["row1 text", "row2 text"]
    bitmap_data = db.Column(db.JSON, nullable=False)  # JSON object: {pixels: "base64...", width: 48, height: 32, rows: 2, cols: 3}
    
    # Relationships
    device = db.relationship('Device', back_populates='display_snapshots')
    
    # Index for efficient time-based queries
    __table_args__ = (
        db.Index('idx_display_device_timestamp', 'device_id', 'timestamp'),
    )
    
    def __repr__(self):
        return f'<DisplaySnapshot {self.device_id} at {self.timestamp}>'
    
    def to_dict(self):
        """Convert display snapshot to dictionary"""
        return {
            'id': self.id,
            'device_id': self.device_id,
            'timestamp': self.timestamp.isoformat(),
            'row_text': self.row_text,
            'bitmap_data': self.bitmap_data
        }
