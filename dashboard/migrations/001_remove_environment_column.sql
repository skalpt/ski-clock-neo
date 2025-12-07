-- Migration: Remove environment column from devices table and dev_environments table
-- Date: 2025-12-07
-- Purpose: Implement database-level environment separation
--          - Dev dashboard connects to dev database (all devices are implicitly dev)
--          - Prod dashboard connects to prod database (all devices are implicitly prod)
--          This eliminates the need for application-level environment filtering

-- Drop the environment column from devices table (no longer needed)
ALTER TABLE devices DROP COLUMN IF EXISTS environment;

-- Drop the dev_environments table (was used for dev-sync functionality, now unused)
DROP TABLE IF EXISTS dev_environments;
