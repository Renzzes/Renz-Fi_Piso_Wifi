import { Router } from "express";
import { sendError } from "../utils/response.js";

export const storageRouter = Router();

storageRouter.post("/retry-sd", (_req, res) => {
  return sendError(res, {
    error: "SD mount failed",
    code: "SD_MOUNT_FAILED",
    status: 500,
  });
});
