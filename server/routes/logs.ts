import { Router } from "express";
import { logsRepository } from "../db/repositories/logsRepository.js";
import { sendSuccess } from "../utils/response.js";

export const logsRouter = Router();

logsRouter.get("/", (req, res) => {
  const q = typeof req.query.q === "string" ? req.query.q : undefined;
  const type = typeof req.query.type === "string" ? req.query.type : undefined;
  return sendSuccess(res, logsRepository.list({ q, type }));
});

logsRouter.delete("/", (_req, res) => {
  logsRepository.clear();
  return sendSuccess(res, { ok: true });
});

logsRouter.get("/export", (req, res) => {
  const q = typeof req.query.q === "string" ? req.query.q : undefined;
  const type = typeof req.query.type === "string" ? req.query.type : undefined;
  const csv = logsRepository.exportCsv({ q, type });
  res.setHeader("Content-Type", "text/csv");
  res.setHeader("Content-Disposition", 'attachment; filename="renz-fi-logs.csv"');
  res.send(csv);
});
