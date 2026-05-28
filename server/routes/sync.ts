import { Router } from "express";
import { getSyncStatus, ingestLogs, ingestTransactions, ingestVouchers } from "../services/sync.js";
import { sendError, sendSuccess } from "../utils/response.js";

export const syncRouter = Router();

syncRouter.get("/", (_req, res) => {
  return sendSuccess(res, getSyncStatus());
});

syncRouter.get("/status", (_req, res) => {
  return sendSuccess(res, getSyncStatus());
});

syncRouter.post("/transactions", (req, res) => {
  const items = Array.isArray(req.body) ? req.body : (req.body?.items ?? [req.body]);
  if (!items.length) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "No transactions provided",
    });
  }
  const result = ingestTransactions(items, req.body?.source ?? "esp32");
  return sendSuccess(res, result);
});

syncRouter.post("/logs", (req, res) => {
  const items = Array.isArray(req.body) ? req.body : [req.body];
  const result = ingestLogs(items, req.body?.source ?? "esp32");
  return sendSuccess(res, result);
});

syncRouter.post("/vouchers", (req, res) => {
  const items = Array.isArray(req.body) ? req.body : [req.body];
  const result = ingestVouchers(items, req.body?.source ?? "esp32");
  return sendSuccess(res, result);
});
