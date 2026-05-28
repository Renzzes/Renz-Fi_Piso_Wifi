import type { Response } from "express";
import type { ApiErrorCode } from "./errors.js";

export function sendSuccess<T>(res: Response, payload: T, message = "OK", status = 200) {
  res.status(status).json({
    success: true,
    data: payload,
    message,
  });
}

export function sendError(
  res: Response,
  payload: { error: string; code?: ApiErrorCode; details?: unknown; status?: number },
) {
  const status = payload.status ?? 400;
  res.status(status).json({
    success: false,
    error: payload.error,
    code: payload.code ?? "INTERNAL_ERROR",
  });
}
