import { Router } from "express";
import { sendSuccess } from "../utils/response.js";

export const rgbRouter = Router();

rgbRouter.get("/status", (_req, res) => {
  return sendSuccess(res, {
    enabled: true,
    mode: "SYSTEM_STATUS",
    state: "IDLE",
    brightness: 80,
    colorName: "BLUE",
    color: { red: 0, green: 0, blue: 255 },
    systemStatus: "HEALTHY",
  });
});

rgbRouter.post("/mode", (_req, res) => sendSuccess(res, { ok: true }));
rgbRouter.post("/color", (_req, res) => sendSuccess(res, { ok: true }));
rgbRouter.post("/brightness", (_req, res) => sendSuccess(res, { ok: true }));
