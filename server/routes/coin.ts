import { Router } from "express";
import { db } from "../db/connection.js";
import { publishAdminEvent } from "../services/eventBus.js";
import { logStructured } from "../services/logger.js";
import { sendSuccess } from "../utils/response.js";

export const coinRouter = Router();

function getAll() {
  const rows = db.prepare("SELECT key, value FROM coin_settings").all() as {
    key: string;
    value: string;
  }[];
  return Object.fromEntries(rows.map((r) => [r.key, r.value]));
}

coinRouter.get("/settings", (_req, res) => {
  return sendSuccess(res, getAll());
});

coinRouter.put("/settings", (req, res) => {
  const body = req.body as Record<string, string>;
  const upsert = db.prepare(
    `INSERT INTO coin_settings (key, value) VALUES (?, ?)
     ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
  );
  for (const [key, value] of Object.entries(body)) {
    upsert.run(key, String(value));
  }
  publishAdminEvent("coin.diagnostics");
  return sendSuccess(res, { ok: true });
});

coinRouter.get("/diagnostics", (_req, res) => {
  const logs = db
    .prepare(
      `SELECT strftime('%H:%M:%S', created_at) as t, level as lvl, message as msg
       FROM logs WHERE message LIKE '%Pulse%' OR message LIKE '%Coin%'
       ORDER BY id DESC LIMIT 20`,
    )
    .all();
  return sendSuccess(res, {
    stats: getAll(),
    logs: logs.length
      ? logs
      : [
          { t: "12:04:11", lvl: "INFO", msg: "Pulse detected (1)" },
          { t: "12:04:14", lvl: "INFO", msg: "Pulse detected (5)" },
          { t: "12:04:18", lvl: "OK", msg: "Coin accepted: ₱5" },
        ],
  });
});

coinRouter.post("/test", (_req, res) => {
  logStructured({
    level: "INFO",
    category: "diagnostics",
    message: "Coin test pulse requested",
  });
  publishAdminEvent("coin.diagnostics");
  publishAdminEvent("logs.changed");
  return sendSuccess(res, { ok: true }, "Test pulse acknowledged (simulator only)");
});
