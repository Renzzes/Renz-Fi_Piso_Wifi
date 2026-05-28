import type { NextFunction, Request, Response } from "express";
import { config } from "../config/index.js";
import { logStructured } from "../services/logger.js";

export function requestLoggerMiddleware(req: Request, res: Response, next: NextFunction) {
  if (!config.logs.requestLogging) return next();

  const start = Date.now();
  res.on("finish", () => {
    const ms = Date.now() - start;
    const requestId = String(res.getHeader("x-request-id") ?? "");
    if (res.statusCode >= 400) {
      logStructured({
        level: res.statusCode >= 500 ? "ERR" : "WARN",
        category: "system",
        message: `${req.method} ${req.path} ${res.statusCode} (${ms}ms)`,
        requestId,
        metadata: { status: res.statusCode, ms, ip: req.ip },
      });
    }
  });
  next();
}
