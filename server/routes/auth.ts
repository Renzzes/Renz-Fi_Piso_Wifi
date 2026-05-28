import { Router } from "express";
import { z } from "zod";
import { config } from "../config/index.js";
import { db } from "../db/connection.js";
import { getCookie } from "../utils/cookies.js";
import { sha256Hex } from "../utils/crypto.js";
import { createAdminSessionToken } from "../services/adminSession.js";
import { verifyAdminPassword } from "../services/adminAuth.js";
import { loginRateLimitMiddleware } from "../middleware/loginRateLimit.js";
import { clearAdminSessionCookie, setAdminSessionCookie } from "../utils/sessionCookie.js";
import { sendError, sendSuccess } from "../utils/response.js";
import { logStructured } from "../services/logger.js";
import { settingsRepository } from "../db/repositories/settingsRepository.js";

export const authRouter = Router();

const loginSchema = z.object({
  password: z.string().min(1).max(256),
  rememberIp: z.boolean().optional(),
});

const changePasswordSchema = z.object({
  oldPassword: z.string().min(1).max(256),
  newPassword: z.string().min(4).max(256),
});

authRouter.post("/login", loginRateLimitMiddleware, async (req, res) => {
  const parsed = loginSchema.safeParse(req.body);
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid login payload",
      details: parsed.error.flatten(),
    });
  }

  const result = await verifyAdminPassword(parsed.data.password);
  if (!result.ok) {
    const code = result.code;
    const messages: Record<string, string> = {
      LOCKED: "Account temporarily locked. Try again later.",
      NO_PASSWORD: "Admin password not configured.",
      INVALID: "Invalid password.",
    };
    return sendError(res, {
      status: code === "LOCKED" ? 423 : 401,
      code,
      error: messages[code] ?? "Login failed",
    });
  }

  const userAgent = String(req.headers["user-agent"] ?? "");
  const ip = String(req.ip ?? req.socket.remoteAddress ?? "");
  const token = createAdminSessionToken({ ip, userAgent });
  setAdminSessionCookie(res, token);

  logStructured({
    level: "INFO",
    category: "auth",
    message: "Admin login successful",
    metadata: { ip, rememberIp: parsed.data.rememberIp ?? false },
    requestId: String(res.getHeader("x-request-id") ?? ""),
  });

  return sendSuccess(res, {
    authenticated: true,
    username: settingsRepository.getAdminUsername(),
    rememberIp: parsed.data.rememberIp ?? false,
    mustChangePassword: parsed.data.password === "admin",
  });
});

authRouter.post("/change-password", async (req, res) => {
  const parsed = changePasswordSchema.safeParse(req.body);
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid password change payload",
      details: parsed.error.flatten(),
    });
  }

  const oldPasswordResult = await verifyAdminPassword(parsed.data.oldPassword);
  if (!oldPasswordResult.ok) {
    return sendError(res, {
      status: 401,
      code: "INVALID",
      error: "Old password is incorrect.",
    });
  }

  await settingsRepository.upsertAdmin({ password: parsed.data.newPassword });
  logStructured({ level: "INFO", category: "auth", message: "Admin password changed" });
  return sendSuccess(res, { ok: true }, "Password changed");
});

authRouter.post("/logout", (req, res) => {
  try {
    const token = getCookie(req, config.adminSessionCookie.name);
    if (token) {
      const tokenHash = sha256Hex(token);
      db.prepare("DELETE FROM admin_sessions WHERE token_hash = ?").run(tokenHash);
    }
  } catch {
    // Best-effort
  }

  clearAdminSessionCookie(res);
  logStructured({ level: "INFO", category: "auth", message: "Admin logout" });
  return sendSuccess(res, { ok: true }, "Logged out");
});
