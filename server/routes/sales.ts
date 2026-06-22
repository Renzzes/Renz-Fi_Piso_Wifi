import { Router } from "express";
import { db } from "../db/connection.js";
import { logStructured } from "../services/logger.js";
import { sendSuccess } from "../utils/response.js";

export const salesRouter = Router();

salesRouter.get("/today", (_req, res) => {
  const today = new Date().toISOString().slice(0, 10);
  return sendSuccess(res, salesAggregateSince(today));
});

salesRouter.get("/weekly", (_req, res) => {
  const weekAgo = new Date(Date.now() - 7 * 86400000).toISOString().slice(0, 10);
  return sendSuccess(res, salesAggregateSince(weekAgo));
});

salesRouter.get("/monthly", (_req, res) => {
  const monthStart = `${new Date().toISOString().slice(0, 8)}01`;
  return sendSuccess(res, salesAggregateSince(monthStart));
});

salesRouter.get("/history", (_req, res) => {
  return sendSuccess(res, salesHistory());
});

salesRouter.get("/export", (_req, res) => {
  const stamp = new Date().toISOString().slice(0, 10);
  const rows = db
    .prepare(
      `SELECT recorded_at, amount, sessions, source, metadata
       FROM sales_transactions
       ORDER BY recorded_at DESC`,
    )
    .all() as Array<{
    recorded_at: string;
    amount: number;
    sessions: number;
    source: string | null;
    metadata: string | null;
  }>;

  const lines = [
    "Date,Time,Amount,Minutes,Voucher,MAC Address,IP Address,Profile,Status",
  ];

  for (const row of rows) {
    const [datePart = "", timePart = ""] = String(row.recorded_at ?? "").split("T");
    let metadata: Record<string, unknown> = {};
    if (row.metadata) {
      try {
        metadata = JSON.parse(row.metadata) as Record<string, unknown>;
      } catch {
        metadata = {};
      }
    }
    const minutes = metadata.minutes ?? metadata.durationMinutes ?? row.sessions ?? "";
    const voucher = row.source === "voucher" ? String(metadata.voucherCode ?? metadata.code ?? "") : "";
    const mac = String(metadata.macAddress ?? metadata.mac ?? "");
    const ip = String(metadata.ipAddress ?? metadata.ip ?? "");
    const profile = String(metadata.profile ?? "");
    lines.push(
      [
        csvCell(datePart),
        csvCell(timePart.replace(/Z$/, "")),
        csvCell(row.amount ?? 0),
        csvCell(minutes),
        csvCell(voucher),
        csvCell(mac),
        csvCell(ip),
        csvCell(profile),
        csvCell("completed"),
      ].join(","),
    );
  }

  logStructured({ level: "INFO", category: "sales", message: "export generated" });
  res.setHeader("Content-Type", "text/csv; charset=utf-8");
  res.setHeader("Content-Disposition", `attachment; filename="sales-report-${stamp}.csv"`);
  res.send(`${lines.join("\n")}\n`);
});

function csvCell(value: unknown) {
  const text = String(value ?? "");
  if (/[",\n]/.test(text)) return `"${text.replace(/"/g, '""')}"`;
  return text;
}

function salesAggregateSince(since: string) {
  return db
    .prepare(
      `SELECT COALESCE(SUM(amount), 0) as amount, COALESCE(SUM(sessions), 0) as sessions
       FROM sales_transactions WHERE date(recorded_at) >= date(?)`,
    )
    .get(since) as { amount: number; sessions: number };
}

function salesHistory() {
  return db
    .prepare(
      `SELECT date(recorded_at) as date, SUM(sessions) as sessions, SUM(amount) as revenue
       FROM sales_transactions GROUP BY date(recorded_at) ORDER BY date DESC LIMIT 30`,
    )
    .all() as { date: string; sessions: number; revenue: number }[];
}

function chartSince(days: number) {
  const rows = salesHistory();
  const cutoff = new Date(Date.now() - days * 86400000).toISOString().slice(0, 10);
  const filtered = rows
    .filter((row) => row.date >= cutoff)
    .sort((a, b) => a.date.localeCompare(b.date));
  return {
    labels: filtered.map((row) => row.date),
    data: filtered.map((row) => row.revenue),
  };
}

salesRouter.get("/chart/daily", (_req, res) => {
  return sendSuccess(res, chartSince(7));
});

salesRouter.get("/chart/weekly", (_req, res) => {
  return sendSuccess(res, chartSince(28));
});

salesRouter.get("/chart/monthly", (_req, res) => {
  return sendSuccess(res, chartSince(180));
});
