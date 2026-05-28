import { db } from "../connection.js";
import { hashPassword } from "../../utils/password.js";

const backupTables = [
  "promo_rates",
  "vouchers",
  "sales_transactions",
  "active_sessions",
  "logs",
  "admin_settings",
  "portal_settings",
  "coin_settings",
  "router_settings",
  "sync_queue",
  "admin_sessions",
] as const;

export const settingsRepository = {
  getAdminUsername: () => {
    const row = db.prepare("SELECT value FROM admin_settings WHERE key = 'username'").get() as
      | { value: string }
      | undefined;
    return row?.value ?? "admin";
  },

  getPasswordHash: () => {
    const row = db.prepare("SELECT value FROM admin_settings WHERE key = 'password_hash'").get() as
      | { value: string }
      | undefined;
    return row?.value ?? "";
  },

  setPasswordHash: (hash: string) => {
    db.prepare(
      `INSERT INTO admin_settings (key, value) VALUES ('password_hash', ?)
       ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
    ).run(hash);
  },

  upsertAdmin: async (input: { username?: string; password?: string }) => {
    const { username, password } = input;
    const upsert = db.prepare(
      `INSERT INTO admin_settings (key, value) VALUES (?, ?)
       ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
    );
    if (username) upsert.run("username", username);
    if (password !== undefined) {
      const hashed = await hashPassword(password);
      upsert.run("password_hash", hashed);
    }
  },

  backup: () => {
    const backup: Record<string, unknown> = { exportedAt: new Date().toISOString() };
    for (const table of backupTables) {
      backup[table] = db.prepare(`SELECT * FROM ${table}`).all();
    }
    return backup;
  },

  restore: (backup: Record<string, unknown>) => {
    const tableColumns: Record<string, string[]> = {
      promo_rates: [
        "id",
        "name",
        "coin",
        "minutes",
        "speed",
        "devices",
        "data_cap_mb",
        "created_at",
      ],
      vouchers: ["id", "code", "amount", "minutes", "status", "expires", "created_at"],
      sales_transactions: [
        "id",
        "amount",
        "sessions",
        "source",
        "external_id",
        "recorded_at",
        "metadata",
      ],
      active_sessions: ["id", "mac", "ip", "remaining", "device", "connected_at"],
      logs: ["id", "level", "message", "created_at", "type", "metadata"],
      admin_settings: ["key", "value"],
      portal_settings: ["key", "value"],
      coin_settings: ["key", "value"],
      router_settings: ["key", "value"],
      sync_queue: [
        "id",
        "source",
        "event_type",
        "dedupe_key",
        "status",
        "payload",
        "created_at",
        "processed_at",
        "error",
      ],
      admin_sessions: [
        "token_hash",
        "created_at",
        "expires_at",
        "ip",
        "user_agent",
        "last_seen_at",
      ],
    };

    const restoreTable = (table: keyof typeof tableColumns, rows: unknown[]) => {
      const cols = tableColumns[table];
      db.prepare(`DELETE FROM ${table}`).run();

      if (!rows.length) return;

      const insert = db.prepare(
        `INSERT INTO ${table} (${cols.join(",")}) VALUES (${cols.map(() => "?").join(",")})`,
      );

      for (const row of rows) {
        if (!row || typeof row !== "object") continue;
        const rec = row as Record<string, unknown>;
        const values = cols.map((c) => (rec[c] === undefined ? null : (rec[c] as unknown)));
        insert.run(...values);
      }
    };

    db.transaction(() => {
      for (const table of backupTables) {
        const rows = backup[table] as unknown[] | undefined;
        if (!Array.isArray(rows)) continue;
        restoreTable(table as keyof typeof tableColumns, rows);
      }
    })();

    return { ok: true, message: "Restore applied" };
  },
};
