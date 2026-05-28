import { Router } from "express";
import { getRouterConfig, saveRouterConfig, testRouterConnection } from "../services/mikrotik.js";
import { z } from "zod";
import { sendError, sendSuccess } from "../utils/response.js";

export const routerRouter = Router();

routerRouter.get("/settings", (_req, res) => {
  return sendSuccess(res, getRouterConfig());
});

routerRouter.put("/settings", (req, res) => {
  const parsed = z
    .object({
      host: z.string().trim().min(1).max(256),
      username: z.string().trim().min(1).max(128),
      password: z.string().trim().max(256),
      profile: z.string().trim().min(1).max(128),
      ssid: z.string().trim().min(1).max(128).optional(),
    })
    .safeParse(req.body);

  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid router settings payload",
      details: parsed.error.flatten(),
    });
  }

  saveRouterConfig(parsed.data);
  return sendSuccess(res, { ok: true });
});

routerRouter.post("/test", async (req, res) => {
  const parsed = z
    .object({
      host: z.string().trim().min(1).max(256).optional(),
      username: z.string().trim().min(1).max(128).optional(),
      password: z.string().trim().max(256).optional(),
      profile: z.string().trim().min(1).max(128).optional(),
      ssid: z.string().trim().min(1).max(128).optional(),
    })
    .safeParse(req.body);

  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid router test payload",
      details: parsed.error.flatten(),
    });
  }

  const config = { ...getRouterConfig(), ...parsed.data };
  const ok = await testRouterConnection(config);
  return sendSuccess(res, { ok });
});
