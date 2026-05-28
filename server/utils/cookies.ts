import type { Request } from "express";

export function parseCookieHeader(cookieHeader: string | undefined) {
  const out: Record<string, string> = {};
  if (!cookieHeader) return out;

  // Example: "a=1; b=hello"
  for (const part of cookieHeader.split(";")) {
    const [k, ...v] = part.trim().split("=");
    if (!k) continue;
    const value = v.join("=").trim();
    if (!value) continue;
    out[k] = decodeURIComponent(value);
  }

  return out;
}

export function getCookie(req: Request, name: string) {
  const cookies = parseCookieHeader(req.headers.cookie);
  return cookies[name] ?? null;
}
