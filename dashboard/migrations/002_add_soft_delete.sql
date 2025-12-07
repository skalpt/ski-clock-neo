-- Migration: Add soft delete support to devices table
-- Run this on production database to add soft delete support

-- Add deleted_at column for soft delete (NULL = active, timestamp = deleted)
ALTER TABLE devices ADD COLUMN IF NOT EXISTS deleted_at TIMESTAMP WITH TIME ZONE;

-- Create index for efficient filtering of non-deleted devices
CREATE INDEX IF NOT EXISTS idx_devices_deleted_at ON devices(deleted_at);
