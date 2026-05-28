import type { NextFunction, Request, Response } from "express";

// eslint-disable-next-line no-control-regex -- intentional strip of control chars from API payloads
const CONTROL_CHARS = /[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g;

function sanitizeValue(value: unknown): unknown {
  if (typeof value === "string") {
    return value.replace(CONTROL_CHARS, "").trim();
  }
  if (Array.isArray(value)) return value.map(sanitizeValue);
  if (value && typeof value === "object") {
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(value as Record<string, unknown>)) {
      out[k] = sanitizeValue(v);
    }
    return out;
  }
  return value;
}

export function sanitizeBodyMiddleware(req: Request, _res: Response, next: NextFunction) {
  if (req.body && typeof req.body === "object") {
    req.body = sanitizeValue(req.body);
  }
  next();
}
