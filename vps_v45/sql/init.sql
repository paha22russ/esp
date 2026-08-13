CREATE TABLE IF NOT EXISTS devices (
  id TEXT PRIMARY KEY,
  name TEXT,
  created_at TIMESTAMPTZ DEFAULT now()
);

CREATE TABLE IF NOT EXISTS telemetry (
  id BIGSERIAL PRIMARY KEY,
  ts TIMESTAMPTZ NOT NULL DEFAULT now(),
  device_id TEXT NOT NULL,
  payload JSONB NOT NULL
);
CREATE INDEX IF NOT EXISTS telemetry_device_ts ON telemetry (device_id, ts DESC);

CREATE TABLE IF NOT EXISTS user_events (
  id BIGSERIAL PRIMARY KEY,
  ts TIMESTAMPTZ NOT NULL DEFAULT now(),
  device_id TEXT NOT NULL,
  event TEXT NOT NULL,
  text TEXT,
  audio_uri TEXT,
  snapshot JSONB
);

CREATE TABLE IF NOT EXISTS commands (
  id BIGSERIAL PRIMARY KEY,
  created_at TIMESTAMPTZ DEFAULT now(),
  device_id TEXT NOT NULL,
  cmd JSONB NOT NULL,
  consumed BOOLEAN DEFAULT FALSE
);
