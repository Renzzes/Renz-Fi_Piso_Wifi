import { Router } from "express";
import { logsRepository } from "../db/repositories/logsRepository.js";
import { clearRamLogs, exportRamLogs, listRamLogs, pushRamLog } from "../services/ramLogs.js";
import { sendSuccess } from "../utils/response.js";

export const logsRouter = Router();

logsRouter.get("/", (req, res) => {
  const q = typeof req.query.q === "string" ? req.query.q : undefined;
  const ram = listRamLogs(q);
  if (ram.length > 0) return sendSuccess(res, ram);
  const dbRows = logsRepository.list({ q });
  return sendSuccess(res, dbRows);
});

logsRouter.delete("/", (_req, res) => {
  clearRamLogs();
  return sendSuccess(res, { ok: true });
});

logsRouter.get("/export", (_req, res) => {
  res.setHeader("Content-Type", "application/json");
  res.setHeader("Content-Disposition", 'attachment; filename="renz-fi-logs.json"');
  res.send(exportRamLogs());
});

logsRouter.post("/demo", (_req, res) => {
  const row = pushRamLog({
    lvl: "INFO",
    type: "system",
    msg: "Demo log entry",
  });
  return sendSuccess(res, row);
});
