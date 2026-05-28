import { Router, type Request, type Response } from "express";
import { db } from "../db/connection.js";
import { z } from "zod";
import { sendError, sendSuccess } from "../utils/response.js";

export const portalRouter = Router();

function getAll() {
  const rows = db.prepare("SELECT key, value FROM portal_settings").all() as {
    key: string;
    value: string;
  }[];
  return Object.fromEntries(rows.map((r) => [r.key, r.value]));
}

function set(key: string, value: string) {
  db.prepare(
    `INSERT INTO portal_settings (key, value) VALUES (?, ?)
     ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
  ).run(key, value);
}

function readPortalSettings(_req: Request, res: Response) {
  return sendSuccess(res, getAll());
}

function updatePortalSettings(req: Request, res: Response) {
  const schema = z
    .object({
      portal_name: z.string().max(128).optional(),
      banner: z.string().max(2_000_000).optional(),
    })
    .strict()
    .partial();

  const parsed = schema.safeParse(req.body);
  if (!parsed.success) {
    return sendError(res, {
      status: 400,
      code: "BAD_REQUEST",
      error: "Invalid captive portal payload",
      details: parsed.error.flatten(),
    });
  }

  for (const [key, value] of Object.entries(parsed.data)) {
    if (typeof value === "string") set(key, value);
  }

  return sendSuccess(res, { ok: true });
}

function previewPortalSettings(_req: Request, res: Response) {
  const settings = getAll();
  return sendSuccess(res, {
    portalName: settings.portal_name ?? "Renz-Fi Hotspot",
    welcomeMessage: settings.welcome_message ?? "",
    announcement: settings.announcement ?? "",
    primaryColor: settings.primary_color ?? "#1e293b",
    banner: settings.banner ?? "",
  });
}

portalRouter.get("/", readPortalSettings);
portalRouter.get("/settings", readPortalSettings);
portalRouter.put("/", updatePortalSettings);
portalRouter.put("/settings", updatePortalSettings);
portalRouter.get("/preview", previewPortalSettings);
