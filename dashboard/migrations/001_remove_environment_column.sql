-- Migration: Remove environment column from devices table
-- Date: 2025-12-07
-- Purpose: Implement database-level environment separation
--          - Dev dashboard connects to dev database (all devices are implicitly dev)
--          - Prod dashboard connects to prod database (all devices are implicitly prod)
--          This eliminates the need for application-level environment filtering

-- Drop the environment column from devices table (no longer needed)
ALTER TABLE devices DROP COLUMN IF EXISTS environment;

-- NOTE: The dev_environments table is RETAINED - it's used for dev environment
-- registration so that GitHub Actions knows where to upload firmware builds.
-- Both environments share the same object storage for firmware binaries.
