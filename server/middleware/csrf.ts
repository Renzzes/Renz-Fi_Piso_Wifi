import type { NextFunction, Request, Response } from "express";
import { config } from "../config/index.js";
import { getCookie } from "../utils/cookies.js";
import { generateRandomToken } from "../utils/crypto.js";

/** CSRF-ready double-submit cookie (disabled by default via CSRF_ENABLED). */
export function csrfMiddleware(req: Request, res: Response, next: NextFunction) {
  if (!config.csrf.enabled) return next();

  const safeMethods = new Set(["GET", "HEAD", "OPTIONS"]);
  if (safeMethods.has(req.method)) {
    const existing = getCookie(req, config.csrf.cookieName);
    if (!existing) {
      const token = generateRandomToken(16);
      const parts = [
        `${encodeURIComponent(config.csrf.cookieName)}=${encodeURIComponent(token)}`,
        "Path=/",
        "SameSite=Strict",
      ];
      if (config.isProd) parts.push("Secure");
      res.setHeader("Set-Cookie", parts.join("; "));
    }
    return next();
  }

  const cookieToken = getCookie(req, config.csrf.cookieName);
  const headerToken = req.header("x-csrf-token");
  if (!cookieToken || !headerToken || cookieToken !== headerToken) {
    return res.status(403).json({ success: false, error: "CSRF validation failed", code: "CSRF" });
  }
  next();
}
