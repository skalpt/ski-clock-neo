"""
Object Storage integration using Replit's official client library.
Provides a thin wrapper around replit.object_storage for firmware persistence.
"""

import os
from pathlib import Path
from typing import Optional
from replit.object_storage import Client
from replit.object_storage.errors import ObjectNotFoundError as ReplitObjectNotFoundError


class ObjectStorageError(Exception):
    """Raised when Object Storage operations fail"""
    pass


class ObjectNotFoundError(Exception):
    """Raised when requested object doesn't exist"""
    pass


class ObjectStorageService:
    """
    Service for interacting with Replit Object Storage (Google Cloud Storage backend).
    
    Uses the official replit.object_storage library which handles authentication automatically.
    """
    
    def __init__(self):
        """
        Initialize Object Storage client.
        
        Raises:
            ObjectStorageError: If OBJECT_STORAGE_BUCKET is not set
        """
        bucket_name = os.environ.get('OBJECT_STORAGE_BUCKET')
        if not bucket_name:
            raise ObjectStorageError(
                "OBJECT_STORAGE_BUCKET environment variable not set. "
                "Create a bucket in the Object Storage tool and set this variable."
            )
        
        self.bucket_name = bucket_name
        
        try:
            # Official Replit client handles authentication automatically
            self.client = Client()
        except Exception as e:
            raise ObjectStorageError(f"Failed to initialize Object Storage client: {e}")
    
    def get_bucket_name(self) -> str:
        """Get the configured bucket name"""
        return self.bucket_name
    
    def upload_file(self, local_path: Path, object_name: str) -> str:
        """
        Upload a file to Object Storage.
        
        Args:
            local_path: Path to local file
            object_name: Destination name in bucket (e.g., 'firmwares/file.bin')
        
        Returns:
            Full object path (e.g., '/bucket-name/firmwares/file.bin')
        
        Raises:
            ObjectStorageError: If upload fails
        """
        try:
            # Read file and upload as bytes
            with open(local_path, 'rb') as f:
                file_data = f.read()
            
            self.client.upload_from_bytes(object_name, file_data)
            
            # Return full path for metadata storage
            return f"/{self.bucket_name}/{object_name}"
        
        except Exception as e:
            raise ObjectStorageError(f"Failed to upload file: {e}")
    
    def download_as_bytes(self, object_path: str) -> bytes:
        """
        Download an object as bytes.
        
        Args:
            object_path: Full path (e.g., '/bucket-name/firmwares/file.bin')
                        or object name (e.g., 'firmwares/file.bin')
        
        Returns:
            File contents as bytes
        
        Raises:
            ObjectNotFoundError: If object doesn't exist
            ObjectStorageError: If download fails
        """
        try:
            # Extract object name from full path if needed
            object_name = object_path
            if object_path.startswith('/'):
                # Remove leading slash and bucket name
                parts = object_path.lstrip('/').split('/', 1)
                if len(parts) > 1:
                    object_name = parts[1]
                else:
                    object_name = parts[0]
            
            return self.client.download_as_bytes(object_name)
        
        except ReplitObjectNotFoundError:
            raise ObjectNotFoundError(f"Object not found: {object_path}")
        except Exception as e:
            raise ObjectStorageError(f"Failed to download object: {e}")
    
    def download_as_string(self, object_path: str) -> str:
        """
        Download an object as UTF-8 string.
        
        Args:
            object_path: Full path or object name
        
        Returns:
            File contents as string
        
        Raises:
            ObjectNotFoundError: If object doesn't exist
            ObjectStorageError: If download fails
        """
        try:
            # Extract object name from full path if needed
            object_name = object_path
            if object_path.startswith('/'):
                parts = object_path.lstrip('/').split('/', 1)
                if len(parts) > 1:
                    object_name = parts[1]
                else:
                    object_name = parts[0]
            
            return self.client.download_as_text(object_name)
        
        except ReplitObjectNotFoundError:
            raise ObjectNotFoundError(f"Object not found: {object_path}")
        except Exception as e:
            raise ObjectStorageError(f"Failed to download object: {e}")
    
    def exists(self, object_name: str) -> bool:
        """
        Check if an object exists.
        
        Args:
            object_name: Name of the object (e.g., 'firmwares/file.bin')
        
        Returns:
            True if object exists, False otherwise
        """
        try:
            return self.client.exists(object_name)
        except Exception:
            return False
    
    def upload_string(self, object_name: str, content: str) -> str:
        """
        Upload a string as UTF-8 text.
        
        Args:
            object_name: Destination name in bucket
            content: String content to upload
        
        Returns:
            Full object path
        
        Raises:
            ObjectStorageError: If upload fails
        """
        try:
            self.client.upload_from_text(object_name, content)
            return f"/{self.bucket_name}/{object_name}"
        
        except Exception as e:
            raise ObjectStorageError(f"Failed to upload string: {e}")
