import type { Request, Response } from "express";

export function notFoundApiHandler(_req: Request, res: Response) {
  res.status(404).json({
    success: false,
    error: "Not Found",
    code: "NOT_FOUND",
  });
}
