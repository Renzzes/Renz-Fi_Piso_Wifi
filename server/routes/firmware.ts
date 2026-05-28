import { Router, type Request, type Response } from "express";
import { z } from "zod";
import { sendError, sendSuccess } from "../utils/response.js";
import { handleFirmwareUpload } from "../services/firmware.js";

export const firmwareRouter = Router();

firmwareRouter.get("/", (_req, res) => {
  return sendSuccess(res, {
    version: "v1.0.0",
    build: "2026-05-20",
    sizeMb: 1.2,
    note: "Firmware metadata only — ESP32 OTA is handled externally",
  });
});

const firmwareUploadSchema = z
  .object({
    // Current UI uploads are not wired to real OTA yet.
    filename: z.string().min(1).optional(),
    sizeBytes: z.number().int().nonnegative().optional(),
    mimeType: z.string().optional(),
  })
  .partial();

function uploadFirmware(req: Request, res: Response) {
  const parsed = firmwareUploadSchema.safeParse(req.body ?? {});
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid firmware upload payload",
      details: parsed.error.flatten(),
    });
  }

  return sendSuccess(res, handleFirmwareUpload(parsed.data));
}

firmwareRouter.post("/", uploadFirmware);
firmwareRouter.post("/upload", uploadFirmware);
