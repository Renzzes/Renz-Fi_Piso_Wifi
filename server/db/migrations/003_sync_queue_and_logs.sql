-- Sync queue for pending ingestion (dedupe-ready) and structured log fields.

CREATE TABLE IF NOT EXISTS sync_queue (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  source TEXT NOT NULL DEFAULT 'esp32',
  event_type TEXT NOT NULL,
  dedupe_key TEXT,
  status TEXT NOT NULL DEFAULT 'pending', -- pending | applied | failed
  payload TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT (datetime('now')),
  processed_at TEXT,
  error TEXT
);

CREATE INDEX IF NOT EXISTS idx_sync_queue_status ON sync_queue(status);
CREATE INDEX IF NOT EXISTS idx_sync_queue_event_type ON sync_queue(event_type);

-- Dedupe-ready unique key (optional).
CREATE UNIQUE INDEX IF NOT EXISTS idx_sync_queue_dedupe_key
  ON sync_queue(dedupe_key)
  WHERE dedupe_key IS NOT NULL;

-- Structured logs (backwards compatible with the existing columns).
ALTER TABLE logs ADD COLUMN type TEXT NOT NULL DEFAULT 'system';
ALTER TABLE logs ADD COLUMN metadata TEXT;

