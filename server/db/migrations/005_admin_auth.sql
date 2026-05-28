-- Admin auth metadata (failed logins, lockout tracking).

CREATE TABLE IF NOT EXISTS admin_auth_state (
  id INTEGER PRIMARY KEY CHECK (id = 1),
  failed_attempts INTEGER NOT NULL DEFAULT 0,
  locked_until TEXT,
  last_failed_at TEXT,
  last_success_at TEXT
);

INSERT OR IGNORE INTO admin_auth_state (id, failed_attempts) VALUES (1, 0);
