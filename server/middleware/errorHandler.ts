import type { ErrorRequestHandler } from "express";
import { ApiError } from "../utils/errors.js";
import { sendError } from "../utils/response.js";

export const errorHandler: ErrorRequestHandler = (err, _req, res, _next) => {
  console.error(err);

  if (err instanceof ApiError) {
    return sendError(res, {
      error: err.message,
      code: err.code,
      status: err.status,
    });
  }

  // Fallback: keep error messages safe (avoid leaking internals).
  return sendError(res, {
    error:
      err?.message && process.env.NODE_ENV !== "production" ? err.message : "Internal server error",
    code: "INTERNAL_ERROR",
    status: 500,
  });
};
