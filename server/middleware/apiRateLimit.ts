import rateLimit from "express-rate-limit";
import { config } from "../config/index.js";

// Keep rate limits high enough to avoid bricking local operations/polling.
export const apiRateLimitMiddleware = rateLimit({
  windowMs: config.rateLimit.windowMs,
  max: config.rateLimit.max,
  standardHeaders: true,
  legacyHeaders: false,
  handler: (_req, res) => {
    res.status(429).json({
      success: false,
      error: "Rate limit exceeded",
      code: "RATE_LIMITED",
    });
  },
});
