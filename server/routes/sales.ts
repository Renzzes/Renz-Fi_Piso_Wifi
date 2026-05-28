import { Router } from "express";
import { salesRepository } from "../db/repositories/salesRepository.js";
import { sendSuccess } from "../utils/response.js";

export const salesRouter = Router();

salesRouter.get("/today", (_req, res) => {
  const today = new Date().toISOString().slice(0, 10);
  return sendSuccess(res, salesRepository.aggregateSince(today));
});

salesRouter.get("/weekly", (_req, res) => {
  const weekAgo = new Date(Date.now() - 7 * 86400000).toISOString().slice(0, 10);
  return sendSuccess(res, salesRepository.aggregateSince(weekAgo));
});

salesRouter.get("/monthly", (_req, res) => {
  const monthStart = `${new Date().toISOString().slice(0, 8)}01`;
  return sendSuccess(res, salesRepository.aggregateSince(monthStart));
});

salesRouter.get("/history", (_req, res) => {
  return sendSuccess(res, salesRepository.history());
});

salesRouter.get("/export", (_req, res) => {
  const rows = salesRepository.history();
  const csv = [
    "date,sessions,revenue",
    ...rows.map((row) => `${row.date},${row.sessions},${row.revenue}`),
  ].join("\n");
  res.setHeader("Content-Type", "text/csv");
  res.setHeader("Content-Disposition", 'attachment; filename="renz-fi-sales.csv"');
  res.send(csv);
});

salesRouter.get("/chart/daily", (_req, res) => {
  return sendSuccess(res, {
    labels: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"],
    data: [12, 28, 18, 42, 35, 56, 48],
  });
});

salesRouter.get("/chart/weekly", (_req, res) => {
  return sendSuccess(res, { labels: ["W1", "W2", "W3", "W4"], data: [180, 220, 195, 260] });
});

salesRouter.get("/chart/monthly", (_req, res) => {
  return sendSuccess(res, {
    labels: ["Jan", "Feb", "Mar", "Apr", "May", "Jun"],
    data: [1820, 2100, 1980, 2400, 2680, 2950],
  });
});
