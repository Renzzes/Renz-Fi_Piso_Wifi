import fs from "node:fs";
import path from "node:path";
import { Router, type Request, type Response } from "express";
import express from "express";
import { sendError, sendSuccess } from "../utils/response.js";
import { publishAdminEvent } from "../services/eventBus.js";
import { logStructured } from "../services/logger.js";

export const portalRouter = Router();
export const portalPublicRouter = Router();

const uploadDir = path.join(process.cwd(), "server", "data", "portal");
fs.mkdirSync(uploadDir, { recursive: true });

const bannerPath = path.join(uploadDir, "portal-banner.webp");
const musicPath = path.join(uploadDir, "portal-bg-music.mp3");

type PortalMeta = {
  revision: number;
  has_banner: boolean;
  has_music: boolean;
  bannerConfigured: boolean;
  musicConfigured: boolean;
  bannerUrl?: string;
  musicUrl?: string;
  banner_path?: string;
  music_path?: string;
};

const metaPath = path.join(uploadDir, "portal.json");

function readMeta(): PortalMeta {
  try {
    const parsed = JSON.parse(fs.readFileSync(metaPath, "utf8")) as PortalMeta;
    return {
      ...parsed,
      bannerConfigured: Boolean(parsed.bannerConfigured ?? parsed.has_banner),
      musicConfigured: Boolean(parsed.musicConfigured ?? parsed.has_music),
    };
  } catch {
    return {
      revision: 0,
      has_banner: false,
      has_music: false,
      bannerConfigured: false,
      musicConfigured: false,
    };
  }
}

function writeMeta(meta: PortalMeta) {
  const payload = {
    ...meta,
    has_banner: meta.bannerConfigured,
    has_music: meta.musicConfigured,
    bannerUrl: meta.bannerConfigured ? "/api/portal/assets/banner?v=" + meta.revision : undefined,
    musicUrl: meta.musicConfigured ? "/api/portal/assets/music?v=" + meta.revision : undefined,
  };
  fs.writeFileSync(metaPath, JSON.stringify(payload, null, 2));
}

function readPortalSettings(_req: Request, res: Response) {
  return sendSuccess(res, readMeta());
}

function readPortalBranding(_req: Request, res: Response) {
  const meta = readMeta();
  return sendSuccess(res, {
    revision: meta.revision,
    hasCustomBanner: meta.bannerConfigured,
    hasCustomMusic: meta.musicConfigured,
    bannerUrl: meta.bannerUrl,
    musicUrl: meta.musicUrl,
  });
}

portalRouter.get("/", readPortalSettings);
portalRouter.get("/settings", readPortalSettings);

portalPublicRouter.get("/branding", readPortalBranding);

portalPublicRouter.get("/assets/banner", (_req, res) => {
  if (!fs.existsSync(bannerPath)) {
    return sendError(res, { status: 404, code: "NOT_FOUND", error: "Banner not found" });
  }
  res.type("image/webp");
  return res.sendFile(bannerPath);
});

portalPublicRouter.get("/assets/music", (_req, res) => {
  if (!fs.existsSync(musicPath)) {
    return sendError(res, { status: 404, code: "NOT_FOUND", error: "Music not found" });
  }
  res.type("audio/mpeg");
  return res.sendFile(musicPath);
});

portalRouter.post(
  "/banner",
  express.raw({ type: ["image/webp", "image/png", "image/jpeg", "application/octet-stream"], limit: "200kb" }),
  (req, res) => {
    const body = req.body as Buffer;
    if (!body?.length) {
      return sendError(res, { status: 400, code: "BAD_REQUEST", error: "Missing banner body" });
    }
    const target = bannerPath;
    if (fs.existsSync(target)) fs.unlinkSync(target);
    fs.writeFileSync(target, body);
    const meta = readMeta();
    meta.has_banner = true;
    meta.bannerConfigured = true;
    meta.banner_path = target;
    meta.revision = Date.now();
    writeMeta(meta);
    publishAdminEvent("portal.changed", { revision: meta.revision });
    logStructured({ level: "OK", category: "captive_portal", message: "Portal banner uploaded" });
    return sendSuccess(res, readMeta(), "Banner uploaded");
  },
);

portalRouter.post(
  "/music",
  express.raw({ type: ["audio/mpeg", "application/octet-stream"], limit: "1000kb" }),
  (req, res) => {
    const body = req.body as Buffer;
    if (!body?.length) {
      return sendError(res, { status: 400, code: "BAD_REQUEST", error: "Missing music body" });
    }
    const target = musicPath;
    if (fs.existsSync(target)) fs.unlinkSync(target);
    fs.writeFileSync(target, body);
    const meta = readMeta();
    meta.has_music = true;
    meta.musicConfigured = true;
    meta.music_path = target;
    meta.revision = Date.now();
    writeMeta(meta);
    publishAdminEvent("portal.changed", { revision: meta.revision });
    logStructured({ level: "OK", category: "captive_portal", message: "Portal music uploaded" });
    return sendSuccess(res, readMeta(), "Music uploaded");
  },
);

portalRouter.delete("/banner", (_req, res) => {
  if (fs.existsSync(bannerPath)) fs.unlinkSync(bannerPath);
  const meta = readMeta();
  meta.has_banner = false;
  meta.bannerConfigured = false;
  meta.revision = Date.now();
  delete meta.banner_path;
  delete meta.bannerUrl;
  writeMeta(meta);
  publishAdminEvent("portal.changed", { revision: meta.revision });
  return sendSuccess(res, { ok: true });
});

portalRouter.delete("/music", (_req, res) => {
  if (fs.existsSync(musicPath)) fs.unlinkSync(musicPath);
  const meta = readMeta();
  meta.has_music = false;
  meta.musicConfigured = false;
  meta.revision = Date.now();
  delete meta.music_path;
  delete meta.musicUrl;
  writeMeta(meta);
  publishAdminEvent("portal.changed", { revision: meta.revision });
  return sendSuccess(res, { ok: true });
});
