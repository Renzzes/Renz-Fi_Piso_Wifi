import { Router } from "express";
import { getSystemStatus } from "../services/systemStatus.js";
import { sendError, sendSuccess } from "../utils/response.js";

export const storageRouter = Router();

storageRouter.get("/status", (_req, res) => {
  const status = getSystemStatus();
  return sendSuccess(res, status.storageStatus);
});

storageRouter.post("/retry-sd", (_req, res) => {
  return sendError(res, {
    error: "SD mount failed",
    code: "SD_MOUNT_FAILED",
    status: 500,
  });
});
