import type { NextFunction, Request, Response } from "express";
import crypto from "node:crypto";

const HEADER_NAME = "x-request-id";

export function requestIdMiddleware(req: Request, res: Response, next: NextFunction) {
  const incoming = req.header(HEADER_NAME);
  const requestId = incoming && incoming.trim() ? incoming.trim() : crypto.randomUUID();
  res.setHeader(HEADER_NAME, requestId);
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  (req as any).requestId = requestId;
  next();
}
