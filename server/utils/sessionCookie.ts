import type { Response } from "express";
import { config } from "../config/index.js";

export function setAdminSessionCookie(res: Response, token: string) {
  const parts = [
    `${encodeURIComponent(config.adminSessionCookie.name)}=${encodeURIComponent(token)}`,
    "HttpOnly",
    "Path=/",
    "SameSite=Strict",
    `Max-Age=${config.adminSessionCookie.ttlSeconds}`,
  ];
  if (config.isProd) parts.push("Secure");
  res.setHeader("Set-Cookie", parts.join("; "));
}

export function clearAdminSessionCookie(res: Response) {
  const parts = [`${encodeURIComponent(config.adminSessionCookie.name)}=`];
  parts.push("HttpOnly");
  parts.push("Path=/");
  parts.push("SameSite=Strict");
  parts.push("Max-Age=0");
  if (config.isProd) parts.push("Secure");
  res.setHeader("Set-Cookie", parts.join("; "));
}
