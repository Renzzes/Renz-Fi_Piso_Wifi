import path from "node:path";
import { getDataDir } from "../paths.js";

function envNumber(key: string, fallback: number) {
  const raw = process.env[key];
  if (!raw) return fallback;
  const n = Number(raw);
  return Number.isFinite(n) ? n : fallback;
}

function envBool(key: string, fallback: boolean) {
  const raw = process.env[key];
  if (raw === undefined) return fallback;
  return raw === "1" || raw.toLowerCase() === "true";
}

function envList(key: string): string[] {
  const raw = process.env[key];
  if (!raw?.trim()) return [];
  return raw
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
}

export const config = {
  port: envNumber("PORT", 3001),
  isProd: process.env.NODE_ENV === "production",
  isDev: process.env.NODE_ENV !== "production",

  apiJsonLimit: process.env.API_JSON_LIMIT ?? "2mb",
  dataDir: getDataDir(process.env.DATA_DIR),

  rateLimit: {
    windowMs: 60_000,
    max: envNumber("API_RATE_LIMIT_MAX", 60),
  },

  auth: {
    maxFailedAttempts: envNumber("ADMIN_MAX_FAILED_ATTEMPTS", 5),
    lockoutMinutes: envNumber("ADMIN_LOCKOUT_MINUTES", 15),
    loginRateLimitMax: envNumber("ADMIN_LOGIN_RATE_LIMIT_MAX", 10),
    loginRateLimitWindowMs: envNumber("ADMIN_LOGIN_RATE_LIMIT_WINDOW_MS", 60_000),
  },

  adminSessionCookie: {
    name: process.env.ADMIN_SESSION_COOKIE_NAME ?? "renz_admin_session",
    ttlSeconds: envNumber("ADMIN_SESSION_TTL_SECONDS", 24 * 60 * 60),
  },

  csrf: {
    enabled: envBool("CSRF_ENABLED", false),
    cookieName: process.env.CSRF_COOKIE_NAME ?? "renz_csrf",
  },

  lan: {
    subnetRestrictionEnabled: envBool("LAN_SUBNET_RESTRICTION", false),
    allowedSubnets: envList("LAN_ALLOWED_SUBNETS"),
    ipAllowlistEnabled: envBool("LAN_IP_ALLOWLIST", false),
    allowedIps: envList("LAN_ALLOWED_IPS"),
  },

  db: {
    busyTimeoutMs: envNumber("DB_BUSY_TIMEOUT_MS", 5000),
    backupRetention: envNumber("DB_BACKUP_RETENTION", 7),
    autoBackupHours: envNumber("DB_AUTO_BACKUP_HOURS", 24),
    logRetentionDays: envNumber("LOG_RETENTION_DAYS", 30),
  },

  logs: {
    requestLogging: envBool("REQUEST_LOGGING", true),
  },
};

export function getBackupDir() {
  return path.join(config.dataDir, "backups");
}
