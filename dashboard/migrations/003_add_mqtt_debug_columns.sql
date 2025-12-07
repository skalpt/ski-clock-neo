-- Migration: Add MQTT topic and payload columns to event_logs for debugging
-- These columns store the raw MQTT data for troubleshooting

ALTER TABLE event_logs ADD COLUMN IF NOT EXISTS mqtt_topic VARCHAR(256);
ALTER TABLE event_logs ADD COLUMN IF NOT EXISTS mqtt_payload TEXT;

-- Note: Existing events will have NULL values for these columns, which is fine
-- Only new events will have the topic and payload stored
