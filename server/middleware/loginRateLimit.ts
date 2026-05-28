import rateLimit from "express-rate-limit";
import { config } from "../config/index.js";

export const loginRateLimitMiddleware = rateLimit({
  windowMs: config.auth.loginRateLimitWindowMs,
  max: config.auth.loginRateLimitMax,
  standardHeaders: true,
  legacyHeaders: false,
  skip: (req) => {
    const ip = req.ip ?? "";
    return ip === "127.0.0.1" || ip === "::1" || ip === "::ffff:127.0.0.1";
  },
  handler: (_req, res) => {
    res.status(429).json({
      success: false,
      error: "Too many login attempts. Try again later.",
      code: "LOGIN_RATE_LIMITED",
    });
  },
});
