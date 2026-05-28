import { Router, type Request, type Response } from "express";
import { db } from "../db/connection.js";
import { disconnectUser } from "../services/mikrotik.js";
import { publishAdminEvent } from "../services/eventBus.js";
import { sendError, sendSuccess } from "../utils/response.js";

export const usersRouter = Router();

function listActiveUsers(_req: Request, res: Response) {
  const rows = db
    .prepare(`SELECT mac, ip, remaining, device FROM active_sessions ORDER BY connected_at DESC`)
    .all();
  return sendSuccess(res, rows);
}

usersRouter.get("/", listActiveUsers);
usersRouter.get("/active", listActiveUsers);

usersRouter.post("/disconnect", async (req, res) => {
  const { mac } = req.body;
  if (!mac) return sendError(res, { status: 400, code: "BAD_REQUEST", error: "mac required" });
  const ok = await disconnectUser(mac);
  publishAdminEvent("users.active");
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok });
});
