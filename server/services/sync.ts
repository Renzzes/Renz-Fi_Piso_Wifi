import { db } from "../db/connection.js";
import { sha256Hex } from "../utils/crypto.js";
import { publishAdminEvent } from "./eventBus.js";
import { logStructured } from "./logger.js";

export type SyncTransactionPayload = {
  externalId?: string;
  amount: number;
  sessions?: number;
  recordedAt?: string;
  metadata?: Record<string, unknown>;
};

export function getSyncStatus() {
  const pending = (
    db.prepare("SELECT COUNT(*) as c FROM sync_queue WHERE status = 'pending'").get() as {
      c: number;
    }
  ).c;

  const lastBatch = db.prepare(`SELECT * FROM sync_batches ORDER BY id DESC LIMIT 1`).get() as
    | Record<string, unknown>
    | undefined;

  return { pending, lastBatch: lastBatch ?? null };
}

export function ingestTransactions(items: SyncTransactionPayload[], source = "esp32") {
  const batch = db
    .prepare(`INSERT INTO sync_batches (source, status, payload_count) VALUES (?, 'pending', ?)`)
    .run(source, items.length);
  const batchId = Number(batch.lastInsertRowid);

  const dedupeKeys = items.map((item) => {
    const payload = JSON.stringify(item);
    const stablePart = item.externalId ? item.externalId : sha256Hex(payload);
    return `transaction:${stablePart}`;
  });

  const tx = db.transaction(() => {
    const insertQueue = db.prepare(
      `INSERT OR IGNORE INTO sync_queue (source, event_type, dedupe_key, payload, status)
       VALUES (?, 'transaction', ?, ?, 'pending')`,
    );

    for (let i = 0; i < items.length; i++) {
      const payload = JSON.stringify(items[i]);
      insertQueue.run(source, dedupeKeys[i], payload);
    }

    // Apply items (synchronous for now; queue supports future async processing).
    const insertTx = db.prepare(
      `INSERT OR IGNORE INTO sales_transactions (amount, sessions, source, external_id, recorded_at, metadata)
       VALUES (?, ?, ?, ?, COALESCE(?, datetime('now')), ?)`,
    );

    for (const item of items) {
      insertTx.run(
        item.amount,
        item.sessions ?? 1,
        source,
        item.externalId ?? null,
        item.recordedAt ?? null,
        item.metadata ? JSON.stringify(item.metadata) : null,
      );
    }

    for (const key of dedupeKeys) {
      db.prepare(
        `UPDATE sync_queue SET status = 'applied', processed_at = datetime('now')
         WHERE dedupe_key = ?`,
      ).run(key);
    }

    db.prepare(
      `UPDATE sync_batches SET status = 'applied', processed_at = datetime('now') WHERE id = ?`,
    ).run(batchId);

    // Compatibility: still write sync_events for older metrics until UI is fully migrated.
    const insertEvent = db.prepare(
      `INSERT INTO sync_events (batch_id, event_type, status, payload)
       VALUES (?, 'transaction', 'applied', ?)`,
    );
    for (const item of items) {
      insertEvent.run(batchId, JSON.stringify(item));
    }
  });

  tx();

  logStructured({
    level: "INFO",
    category: "sync",
    message: `Synced ${items.length} transaction(s) from ${source}`,
    metadata: { source, count: items.length, dedupe: dedupeKeys.length },
  });

  publishAdminEvent("sync.queue");
  publishAdminEvent("sales.changed");
  publishAdminEvent("system.status");

  return { batchId, applied: items.length };
}

export type SyncLogPayload = {
  level?: string;
  message?: string;
  metadata?: Record<string, unknown>;
};

export function ingestLogs(items: SyncLogPayload[], source = "esp32") {
  const batch = db
    .prepare(`INSERT INTO sync_batches (source, status, payload_count) VALUES (?, 'pending', ?)`)
    .run(source, items.length);
  const batchId = Number(batch.lastInsertRowid);

  const dedupeKeys = items.map((item) => {
    const payload = JSON.stringify(item);
    const stablePart = item.message ? item.message : sha256Hex(payload);
    return `log:${stablePart}`;
  });

  db.transaction(() => {
    const insertQueue = db.prepare(
      `INSERT OR IGNORE INTO sync_queue (source, event_type, dedupe_key, payload, status)
       VALUES (?, 'log', ?, ?, 'pending')`,
    );

    for (let i = 0; i < items.length; i++) {
      const payload = JSON.stringify(items[i]);
      insertQueue.run(source, dedupeKeys[i], payload);
    }

    const insertLog = db.prepare(
      `INSERT INTO logs (level, type, message, metadata) VALUES (?, 'sync', ?, ?)`,
    );

    for (const item of items) {
      if (!item?.message) continue;
      insertLog.run(
        item.level ?? "INFO",
        item.message,
        item.metadata ? JSON.stringify(item.metadata) : null,
      );
    }

    for (const key of dedupeKeys) {
      db.prepare(
        `UPDATE sync_queue SET status = 'applied', processed_at = datetime('now')
         WHERE dedupe_key = ?`,
      ).run(key);
    }

    db.prepare(
      `UPDATE sync_batches SET status = 'applied', processed_at = datetime('now') WHERE id = ?`,
    ).run(batchId);

    const insertEvent = db.prepare(
      `INSERT INTO sync_events (batch_id, event_type, status, payload) VALUES (?, 'log', 'applied', ?)`,
    );
    for (const item of items) {
      insertEvent.run(batchId, JSON.stringify(item));
    }
  })();

  publishAdminEvent("logs.changed");
  publishAdminEvent("sync.queue");

  return { batchId, applied: items.length };
}

export type SyncVoucherPayload = {
  code: string;
  amount: number;
  minutes: number;
  status?: "unused" | "active" | "expired";
  expires: string;
};

export function ingestVouchers(items: SyncVoucherPayload[], source = "esp32") {
  const batch = db
    .prepare(`INSERT INTO sync_batches (source, status, payload_count) VALUES (?, 'pending', ?)`)
    .run(source, items.length);
  const batchId = Number(batch.lastInsertRowid);

  const dedupeKeys = items.map((v) => `voucher:${v.code}`);

  db.transaction(() => {
    const insertQueue = db.prepare(
      `INSERT OR IGNORE INTO sync_queue (source, event_type, dedupe_key, payload, status)
       VALUES (?, 'voucher', ?, ?, 'pending')`,
    );

    for (let i = 0; i < items.length; i++) {
      const payload = JSON.stringify(items[i]);
      insertQueue.run(source, dedupeKeys[i], payload);
    }

    const insertVoucher = db.prepare(
      `INSERT OR IGNORE INTO vouchers (code, amount, minutes, status, expires) VALUES (?, ?, ?, ?, ?)`,
    );

    for (const v of items) {
      insertVoucher.run(v.code, v.amount, v.minutes, v.status ?? "unused", v.expires);
    }

    for (const key of dedupeKeys) {
      db.prepare(
        `UPDATE sync_queue SET status = 'applied', processed_at = datetime('now')
         WHERE dedupe_key = ?`,
      ).run(key);
    }

    db.prepare(
      `UPDATE sync_batches SET status = 'applied', processed_at = datetime('now') WHERE id = ?`,
    ).run(batchId);

    const insertEvent = db.prepare(
      `INSERT INTO sync_events (batch_id, event_type, status, payload) VALUES (?, 'voucher', 'applied', ?)`,
    );
    for (const v of items) {
      insertEvent.run(batchId, JSON.stringify(v));
    }
  })();

  logStructured({
    level: "INFO",
    category: "sync",
    message: `Synced ${items.length} voucher(s) from ${source}`,
    metadata: { source, count: items.length },
  });

  publishAdminEvent("vouchers.changed");
  publishAdminEvent("sync.queue");

  return { batchId, applied: items.length };
}
