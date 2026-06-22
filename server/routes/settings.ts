import { Router } from "express";
import { settingsRepository } from "../db/repositories/settingsRepository.js";
import { z } from "zod";
import { logStructured } from "../services/logger.js";
import { sendError, sendSuccess } from "../utils/response.js";

export const settingsRouter = Router();

const updateAdminSchema = z.object({
  username: z.string().trim().min(1).max(64).optional(),
  password: z.string().min(4).max(256).optional(),
});

settingsRouter.get("/admin", (_req, res) => {
  return sendSuccess(res, { username: settingsRepository.getAdminUsername() });
});

settingsRouter.put("/admin", async (req, res) => {
  const { username, password } = req.body;
  const parsed = updateAdminSchema.safeParse({ username, password });
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid admin settings payload",
      details: parsed.error.flatten(),
    });
  }

  await settingsRepository.upsertAdmin({
    username: parsed.data.username,
    password: parsed.data.password,
  });
  return sendSuccess(res, { ok: true });
});

settingsRouter.get("/backup", (_req, res) => {
  logStructured({ level: "INFO", category: "backup", message: "export started" });
  const backup = settingsRepository.backup();
  res.setHeader("Content-Type", "application/json");
  res.setHeader("Content-Disposition", 'attachment; filename="renzfi-backup.json"');
  logStructured({ level: "INFO", category: "backup", message: "export completed" });
  res.json({
    backupVersion: 1,
    firmwareVersion: "simulator",
    exportedAt: backup.exportedAt,
    ...backup,
  });
});

settingsRouter.post("/restore", (req, res) => {
  logStructured({ level: "INFO", category: "restore", message: "restore started" });
  const backupSchema = z
    .object({
      backupVersion: z.number().optional(),
      firmwareVersion: z.string().optional(),
      exportedAt: z.string(),
      promo_rates: z.array(z.record(z.any())).optional(),
      vouchers: z.array(z.record(z.any())).optional(),
      sales_transactions: z.array(z.record(z.any())).optional(),
      active_sessions: z.array(z.record(z.any())).optional(),
      logs: z.array(z.record(z.any())).optional(),
      admin_settings: z.array(z.record(z.any())).optional(),
      portal_settings: z.array(z.record(z.any())).optional(),
      coin_settings: z.array(z.record(z.any())).optional(),
      router_settings: z.array(z.record(z.any())).optional(),
      sync_queue: z.array(z.record(z.any())).optional(),
      admin_sessions: z.array(z.record(z.any())).optional(),
    })
    .strict();

  const parsed = backupSchema.safeParse(req.body);
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid backup payload",
      details: parsed.error.flatten(),
    });
  }

  const result = settingsRepository.restore(parsed.data);
  logStructured({ level: "INFO", category: "restore", message: "restore completed" });
  return sendSuccess(res, { ...result, rebooting: false });
});
