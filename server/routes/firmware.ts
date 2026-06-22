import { Router, type Request, type Response } from "express";
import express from "express";
import crypto from "node:crypto";
import { sendError, sendSuccess } from "../utils/response.js";
import { publishAdminEvent } from "../services/eventBus.js";
import { pushRamLog } from "../services/ramLogs.js";

export const firmwareRouter = Router();

firmwareRouter.get("/", (_req, res) => {
  return sendSuccess(res, {
    version: "0.5.0-dev-simulator",
    build: new Date().toISOString().slice(0, 10),
    sizeMb: 1.2,
    partition: "simulator",
    note: "Development simulator — flash real .bin on ESP32 hardware",
  });
});

firmwareRouter.post(
  "/",
  express.raw({ type: ["application/octet-stream", "application/x-msdownload"], limit: "3mb" }),
  (req: Request, res: Response) => {
    const body = req.body as Buffer;
    if (!body?.length) {
      return sendError(res, {
        status: 400,
        code: "BAD_REQUEST",
        error: "Missing firmware binary body",
      });
    }

    const md5 = crypto.createHash("md5").update(body).digest("hex");
    publishAdminEvent("firmware.progress", { phase: "verify", md5 });
    publishAdminEvent("firmware.progress", { phase: "complete", md5 });
    pushRamLog({
      lvl: "INFO",
      type: "firmware",
      msg: `Simulator accepted firmware (${body.length} bytes, md5=${md5})`,
    });

    return sendSuccess(res, {
      ok: true,
      bytes: body.length,
      md5,
      rebooting: false,
      message: "Simulator recorded firmware upload (no OTA in dev mode)",
    });
  },
);

firmwareRouter.post("/upload", (req, res) => {
  return sendError(res, {
    status: 400,
    code: "BAD_REQUEST",
    error: "Use POST /api/system/firmware with raw .bin body",
  });
});
