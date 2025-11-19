"""
Replit Object Storage service for Python Flask applications.

This module provides integration with Replit's Object Storage, which uses
Google Cloud Storage under the hood. It handles authentication via the
Replit sidecar endpoint and provides simple methods for uploading and
downloading objects.
"""

import os
import requests
from google.cloud import storage
from google.oauth2 import credentials as oauth2_credentials
from pathlib import Path
from typing import Optional

REPLIT_SIDECAR_ENDPOINT = "http://127.0.0.1:1106"


class ObjectStorageError(Exception):
    """Base exception for object storage errors."""
    pass


class ObjectNotFoundError(ObjectStorageError):
    """Raised when an object is not found in storage."""
    pass


def _get_replit_credentials():
    """Get OAuth2 credentials from Replit sidecar endpoint."""
    try:
        # Get the access token from sidecar
        cred_response = requests.get(f"{REPLIT_SIDECAR_ENDPOINT}/credential")
        cred_response.raise_for_status()
        access_token = cred_response.json()["access_token"]
        
        # Create OAuth2 credentials object
        creds = oauth2_credentials.Credentials(token=access_token)
        return creds
    except Exception as e:
        raise ObjectStorageError(f"Failed to get Replit credentials: {e}")


def _parse_object_path(path: str) -> tuple[str, str]:
    """
    Parse object path into bucket name and object name.
    
    Args:
        path: Path in format "/<bucket_name>/<object_name>"
        
    Returns:
        Tuple of (bucket_name, object_name)
    """
    if not path.startswith("/"):
        path = f"/{path}"
    
    parts = path.split("/")
    if len(parts) < 3:
        raise ValueError("Invalid path: must contain at least a bucket name")
    
    bucket_name = parts[1]
    object_name = "/".join(parts[2:])
    
    return bucket_name, object_name


class ObjectStorageService:
    """Service for interacting with Replit Object Storage."""
    
    def __init__(self):
        """Initialize the object storage service with Replit credentials."""
        try:
            credentials = _get_replit_credentials()
            self.client = storage.Client(credentials=credentials, project="")
        except Exception as e:
            raise ObjectStorageError(f"Failed to initialize object storage: {e}")
    
    def get_bucket_name(self) -> str:
        """Get the bucket name from environment variable."""
        bucket_name = os.getenv('OBJECT_STORAGE_BUCKET')
        if not bucket_name:
            raise ObjectStorageError(
                "OBJECT_STORAGE_BUCKET environment variable not set. "
                "Create a bucket in the Object Storage tool and set this variable."
            )
        return bucket_name
    
    def upload_file(self, local_path: Path, object_name: str) -> str:
        """
        Upload a file to object storage.
        
        Args:
            local_path: Path to the local file to upload
            object_name: Name/path for the object in storage
            
        Returns:
            The object path in storage
        """
        try:
            bucket_name = self.get_bucket_name()
            bucket = self.client.bucket(bucket_name)
            blob = bucket.blob(object_name)
            
            blob.upload_from_filename(str(local_path))
            
            return f"/{bucket_name}/{object_name}"
        except Exception as e:
            raise ObjectStorageError(f"Failed to upload file: {e}")
    
    def upload_string(self, content: str, object_name: str) -> str:
        """
        Upload string content to object storage.
        
        Args:
            content: String content to upload
            object_name: Name/path for the object in storage
            
        Returns:
            The object path in storage
        """
        try:
            bucket_name = self.get_bucket_name()
            bucket = self.client.bucket(bucket_name)
            blob = bucket.blob(object_name)
            
            blob.upload_from_string(content)
            
            return f"/{bucket_name}/{object_name}"
        except Exception as e:
            raise ObjectStorageError(f"Failed to upload string: {e}")
    
    def download_to_file(self, object_path: str, local_path: Path):
        """
        Download an object to a local file.
        
        Args:
            object_path: Path to the object in storage (e.g., "/bucket/path/file.bin")
            local_path: Path where to save the downloaded file
        """
        try:
            bucket_name, object_name = _parse_object_path(object_path)
            bucket = self.client.bucket(bucket_name)
            blob = bucket.blob(object_name)
            
            if not blob.exists():
                raise ObjectNotFoundError(f"Object not found: {object_path}")
            
            blob.download_to_filename(str(local_path))
        except ObjectNotFoundError:
            raise
        except Exception as e:
            raise ObjectStorageError(f"Failed to download file: {e}")
    
    def download_as_string(self, object_path: str) -> str:
        """
        Download an object as a string.
        
        Args:
            object_path: Path to the object in storage
            
        Returns:
            The object content as a string
        """
        try:
            bucket_name, object_name = _parse_object_path(object_path)
            bucket = self.client.bucket(bucket_name)
            blob = bucket.blob(object_name)
            
            if not blob.exists():
                raise ObjectNotFoundError(f"Object not found: {object_path}")
            
            return blob.download_as_text()
        except ObjectNotFoundError:
            raise
        except Exception as e:
            raise ObjectStorageError(f"Failed to download as string: {e}")
    
    def download_as_bytes(self, object_path: str) -> bytes:
        """
        Download an object as bytes.
        
        Args:
            object_path: Path to the object in storage
            
        Returns:
            The object content as bytes
        """
        try:
            bucket_name, object_name = _parse_object_path(object_path)
            bucket = self.client.bucket(bucket_name)
            blob = bucket.blob(object_name)
            
            if not blob.exists():
                raise ObjectNotFoundError(f"Object not found: {object_path}")
            
            return blob.download_as_bytes()
        except ObjectNotFoundError:
            raise
        except Exception as e:
            raise ObjectStorageError(f"Failed to download as bytes: {e}")
    
    def exists(self, object_path: str) -> bool:
        """
        Check if an object exists in storage.
        
        Args:
            object_path: Path to the object in storage
            
        Returns:
            True if the object exists, False otherwise
        """
        try:
            bucket_name, object_name = _parse_object_path(object_path)
            bucket = self.client.bucket(bucket_name)
            blob = bucket.blob(object_name)
            
            return blob.exists()
        except Exception:
            return False
    
    def delete(self, object_path: str):
        """
        Delete an object from storage.
        
        Args:
            object_path: Path to the object in storage
        """
        try:
            bucket_name, object_name = _parse_object_path(object_path)
            bucket = self.client.bucket(bucket_name)
            blob = bucket.blob(object_name)
            
            blob.delete()
        except Exception as e:
            raise ObjectStorageError(f"Failed to delete object: {e}")
    
    def get_blob(self, object_path: str):
        """
        Get a blob object for streaming or advanced operations.
        
        Args:
            object_path: Path to the object in storage
            
        Returns:
            Google Cloud Storage Blob object
        """
        try:
            bucket_name, object_name = _parse_object_path(object_path)
            bucket = self.client.bucket(bucket_name)
            blob = bucket.blob(object_name)
            
            if not blob.exists():
                raise ObjectNotFoundError(f"Object not found: {object_path}")
            
            return blob
        except ObjectNotFoundError:
            raise
        except Exception as e:
            raise ObjectStorageError(f"Failed to get blob: {e}")
