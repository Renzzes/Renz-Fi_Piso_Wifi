import type { NextFunction, Request, Response } from "express";
import { config } from "../config/index.js";
import { getCookie } from "../utils/cookies.js";
import { validateAdminSessionToken } from "../services/adminSession.js";
import { clearAdminSessionCookie } from "../utils/sessionCookie.js";

const PUBLIC_PATHS = new Set(["/health", "/auth/login", "/auth/logout"]);

export function adminSessionRequiredMiddleware(req: Request, res: Response, next: NextFunction) {
  const p = req.path;
  if (PUBLIC_PATHS.has(p)) {
    return next();
  }

  const token = getCookie(req, config.adminSessionCookie.name);
  if (!token) {
    return res.status(401).json({ success: false, error: "Unauthorized", code: "UNAUTHORIZED" });
  }

  const session = validateAdminSessionToken(token);
  if (!session) {
    clearAdminSessionCookie(res);
    return res.status(401).json({ success: false, error: "Session expired", code: "UNAUTHORIZED" });
  }

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  (req as any).adminSession = session;
  next();
}
