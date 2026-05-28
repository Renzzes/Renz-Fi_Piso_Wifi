import type { NextFunction, Request, Response } from "express";
import { config } from "../config/index.js";
import { logStructured } from "../services/logger.js";

function normalizeIp(ip: string) {
  return ip.replace(/^::ffff:/, "");
}

function isLocalhost(ip: string) {
  return ip === "127.0.0.1" || ip === "::1" || ip === "localhost";
}

function ipInCidr(ip: string, cidr: string) {
  if (!cidr.includes("/")) return ip === cidr;
  const [base, bitsStr] = cidr.split("/");
  const bits = Number(bitsStr);
  if (!Number.isFinite(bits)) return false;
  const ipParts = ip.split(".").map(Number);
  const baseParts = base.split(".").map(Number);
  if (ipParts.length !== 4 || baseParts.length !== 4) return false;
  const mask = bits === 0 ? 0 : (~0 << (32 - bits)) >>> 0;
  const ipNum = (ipParts[0]! << 24) | (ipParts[1]! << 16) | (ipParts[2]! << 8) | ipParts[3]!;
  const baseNum =
    (baseParts[0]! << 24) | (baseParts[1]! << 16) | (baseParts[2]! << 8) | baseParts[3]!;
  return (ipNum & mask) === (baseNum & mask);
}

export function lanSecurityMiddleware(req: Request, res: Response, next: NextFunction) {
  const ip = normalizeIp(String(req.ip ?? req.socket.remoteAddress ?? ""));

  if (isLocalhost(ip) || config.isDev) {
    return next();
  }

  if (config.lan.ipAllowlistEnabled && config.lan.allowedIps.length > 0) {
    if (!config.lan.allowedIps.includes(ip)) {
      logStructured({
        level: "WARN",
        category: "auth",
        message: "Blocked request from non-allowlisted IP",
        metadata: { ip, path: req.path },
      });
      return res.status(403).json({ success: false, error: "Forbidden", code: "IP_NOT_ALLOWED" });
    }
  }

  if (config.lan.subnetRestrictionEnabled && config.lan.allowedSubnets.length > 0) {
    const allowed = config.lan.allowedSubnets.some((cidr) => ipInCidr(ip, cidr));
    if (!allowed) {
      logStructured({
        level: "WARN",
        category: "auth",
        message: "Blocked request outside allowed subnet",
        metadata: { ip, path: req.path },
      });
      return res
        .status(403)
        .json({ success: false, error: "Forbidden", code: "SUBNET_NOT_ALLOWED" });
    }
  }

  next();
}
