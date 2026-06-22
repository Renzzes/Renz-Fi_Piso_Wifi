import { Router } from "express";
import { db } from "../db/connection.js";
import { getSystemStatus } from "../services/systemStatus.js";
import { createTimestampedBackup, runIntegrityCheck } from "../services/dbHealth.js";
import { logStructured } from "../services/logger.js";
import { publishAdminEvent } from "../services/eventBus.js";
import { sendSuccess } from "../utils/response.js";

export const systemRouter = Router();

const factoryResetTables = [
  "promo_rates",
  "vouchers",
  "sales_transactions",
  "active_sessions",
  "logs",
  "portal_settings",
  "coin_settings",
  "router_settings",
  "sync_queue",
  "admin_sessions",
] as const;

systemRouter.get("/status", (_req, res) => {
  return sendSuccess(res, getSystemStatus());
});

systemRouter.post("/reboot", (_req, res) => {
  logStructured({ level: "WARN", category: "system", message: "System reboot requested" });
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok: true }, "Reboot scheduled (simulator only)");
});

systemRouter.post("/factory-reset", (_req, res) => {
  logStructured({ level: "INFO", category: "system", message: "factory reset started" });
  db.transaction(() => {
    for (const table of factoryResetTables) {
      db.prepare(`DELETE FROM ${table}`).run();
    }
    db.prepare(
      `INSERT INTO admin_settings (key, value) VALUES ('password_hash', '')
       ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
    ).run();
  })();
  logStructured({ level: "INFO", category: "system", message: "factory reset completed" });
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok: true, rebooting: false }, "Factory reset complete (simulator)");
});

systemRouter.post("/backup", async (_req, res) => {
  const backup = await createTimestampedBackup();
  publishAdminEvent("system.status");
  return sendSuccess(res, backup);
});

systemRouter.get("/db-health", (_req, res) => {
  const integrity = runIntegrityCheck();
  return sendSuccess(res, { integrity });
});
