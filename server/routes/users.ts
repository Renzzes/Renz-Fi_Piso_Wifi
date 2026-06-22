import { Router, type Request, type Response } from "express";
import { db } from "../db/connection.js";
import { disconnectUser } from "../services/mikrotik.js";
import { publishAdminEvent } from "../services/eventBus.js";
import { sendError, sendSuccess } from "../utils/response.js";

export const usersRouter = Router();

type ActiveUserRow = {
  mac: string;
  ip: string;
  sessionType: "coin" | "voucher";
  remainingMinutes: number;
  credits: number;
  paused: boolean;
  active: boolean;
  state: "active" | "paused" | "waiting_coin" | "expired";
  source: "portal" | "voucher";
};

function parseRemainingMinutes(remaining: string): number {
  const hours = remaining.match(/(\d+)h/);
  const minutes = remaining.match(/(\d+)m/);
  return (hours ? Number(hours[1]) * 60 : 0) + (minutes ? Number(minutes[1]) : 0);
}

function listActiveUsers(_req: Request, res: Response) {
  const rows = db
    .prepare(
      `SELECT mac, ip, remaining, device, paused FROM active_sessions ORDER BY connected_at DESC`,
    )
    .all() as {
    mac: string;
    ip: string;
    remaining: string;
    device: string;
    paused: number;
  }[];

  const users: ActiveUserRow[] = rows.map((row) => {
    const isVoucher = row.device.toLowerCase().includes("voucher");
    const paused = row.paused === 1;
    return {
      mac: row.mac,
      ip: row.ip,
      sessionType: isVoucher ? "voucher" : "coin",
      remainingMinutes: parseRemainingMinutes(row.remaining),
      credits: 0,
      paused,
      active: true,
      state: paused ? "paused" : "active",
      source: isVoucher ? "voucher" : "portal",
    };
  });

  return sendSuccess(res, users);
}

function requireMac(req: Request, res: Response) {
  const mac = req.body?.mac as string | undefined;
  if (!mac) {
    sendError(res, { status: 400, code: "BAD_REQUEST", error: "mac required" });
    return null;
  }
  return mac;
}

usersRouter.get("/", listActiveUsers);
usersRouter.get("/active", listActiveUsers);

usersRouter.post("/pause", (req, res) => {
  const mac = requireMac(req, res);
  if (!mac) return;
  const row = db.prepare("SELECT mac, paused FROM active_sessions WHERE mac = ?").get(mac) as
    | { mac: string; paused: number }
    | undefined;
  if (!row) {
    return sendError(res, { status: 404, code: "USER_NOT_FOUND", error: "Active user not found" });
  }
  if (row.paused === 1) {
    publishAdminEvent("users.active");
    publishAdminEvent("system.status");
    return sendSuccess(res, { ok: true });
  }
  db.prepare("UPDATE active_sessions SET paused = 1 WHERE mac = ?").run(mac);
  publishAdminEvent("users.active");
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok: true });
});

usersRouter.post("/resume", (req, res) => {
  const mac = requireMac(req, res);
  if (!mac) return;
  const row = db.prepare("SELECT mac, paused FROM active_sessions WHERE mac = ?").get(mac) as
    | { mac: string; paused: number }
    | undefined;
  if (!row) {
    return sendError(res, { status: 404, code: "USER_NOT_FOUND", error: "Active user not found" });
  }
  if (row.paused === 0) {
    publishAdminEvent("users.active");
    publishAdminEvent("system.status");
    return sendSuccess(res, { ok: true });
  }
  db.prepare("UPDATE active_sessions SET paused = 0 WHERE mac = ?").run(mac);
  publishAdminEvent("users.active");
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok: true });
});

usersRouter.post("/disconnect", async (req, res) => {
  const mac = requireMac(req, res);
  if (!mac) return;
  const ok = await disconnectUser(mac);
  publishAdminEvent("users.active");
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok });
});
