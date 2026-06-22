import { db } from "../db/connection.js";
import type { SystemStatus } from "../types.js";

const startTime = Date.now();

function getSetting(table: string, key: string, fallback = ""): string {
  const row = db.prepare(`SELECT value FROM ${table} WHERE key = ?`).get(key) as
    | { value: string }
    | undefined;
  return row?.value ?? fallback;
}

function sumSales(since: string) {
  const row = db
    .prepare(
      `SELECT COALESCE(SUM(amount), 0) as amount, COALESCE(SUM(sessions), 0) as sessions
       FROM sales_transactions WHERE date(recorded_at) >= date(?)`,
    )
    .get(since) as { amount: number; sessions: number };
  return row;
}

export function getSystemStatus(): SystemStatus {
  const today = new Date().toISOString().slice(0, 10);
  const weekAgo = new Date(Date.now() - 7 * 86400000).toISOString().slice(0, 10);
  const monthStart = `${today.slice(0, 8)}01`;

  const todaySales = sumSales(today);
  const weeklySales = sumSales(weekAgo);
  const monthlySales = sumSales(monthStart);

  const activeCount = (
    db.prepare("SELECT COUNT(*) as c FROM active_sessions").get() as { c: number }
  ).c;

  const pausedCount = (
    db.prepare("SELECT COUNT(*) as c FROM active_sessions WHERE paused = 1").get() as {
      c: number;
    }
  ).c;

  const pendingSync = (
    db.prepare("SELECT COUNT(*) as c FROM sync_queue WHERE status = 'pending'").get() as {
      c: number;
    }
  ).c;

  const lastSync = db
    .prepare(
      `SELECT created_at FROM sync_batches WHERE status = 'applied' ORDER BY id DESC LIMIT 1`,
    )
    .get() as { created_at: string } | undefined;

  const host = getSetting("router_settings", "host", "");
  const ssid = getSetting("router_settings", "ssid", "");
  const routerConfigured = host.trim().length > 0;

  let storageOk = true;
  try {
    db.prepare("SELECT 1").get();
  } catch {
    storageOk = false;
  }

  return {
    server: { ok: true, uptimeSeconds: Math.floor((Date.now() - startTime) / 1000) },
    database: { ok: storageOk },
    sales: {
      today: { amount: todaySales.amount, sessions: todaySales.sessions },
      weekly: { amount: weeklySales.amount, sessions: weeklySales.sessions },
      monthly: { amount: monthlySales.amount, sessions: monthlySales.sessions },
    },
    activeUsers: { count: activeCount, paused: pausedCount, idle: 0 },
    mikrotik: { ok: routerConfigured, host, latencyMs: 0 },
    internet: { ok: false, latencyMs: 0, known: false },
    coinSlot: {
      ok: getSetting("coin_settings", "enabled", "1") === "1",
      state: getSetting("coin_settings", "state", ""),
      pulsesToday: Number(getSetting("coin_settings", "total_today", "0") || 0),
    },
    hotspot: { ok: routerConfigured && ssid.trim().length > 0, ssid },
    esp32: {
      uptime: `${Math.floor((Date.now() - startTime) / 1000)}s`,
      lastSeen: new Date().toISOString(),
    },
    storage: {
      flashUsedMb: 0,
      flashTotalMb: 0,
      ramUsedKb: 128,
      ramTotalKb: 320,
      logsUsedKb: 0,
      logsTotalKb: 256,
      sd: {
        present: true,
        mounted: true,
        usedMb: 0,
        totalMb: 512,
        freeMb: 512,
        status: "Ready",
        fallback: false,
        pollingDisabled: false,
        recoveryAttempts: 0,
        mode: "SD",
      },
    },
    sync: { pending: pendingSync, lastSyncAt: lastSync?.created_at ?? null },
  };
}
