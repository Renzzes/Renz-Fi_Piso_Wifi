import { Router } from "express";
import { getSystemStatus } from "../services/systemStatus.js";
import { createTimestampedBackup, runIntegrityCheck } from "../services/dbHealth.js";
import { logStructured } from "../services/logger.js";
import { publishAdminEvent } from "../services/eventBus.js";
import { sendSuccess } from "../utils/response.js";

export const systemRouter = Router();

systemRouter.get("/status", (_req, res) => {
  return sendSuccess(res, getSystemStatus());
});

systemRouter.post("/reboot", (_req, res) => {
  logStructured({ level: "WARN", category: "system", message: "System reboot requested" });
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok: true }, "Reboot scheduled (simulator only)");
});

systemRouter.post("/factory-reset", (_req, res) => {
  logStructured({ level: "ERR", category: "system", message: "Factory reset requested" });
  return sendSuccess(res, { ok: true }, "Factory reset prepared (simulator only)");
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
