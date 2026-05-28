import { db } from "../db/connection.js";
import { config } from "../config/index.js";
import { generateRandomToken, sha256Hex } from "../utils/crypto.js";

export function validateAdminSessionToken(token: string) {
  const tokenHash = sha256Hex(token);
  const row = db
    .prepare(
      `SELECT token_hash, ip, user_agent, expires_at
       FROM admin_sessions
       WHERE token_hash = ?
         AND expires_at > datetime('now')`,
    )
    .get(tokenHash) as
    | {
        token_hash: string;
        ip: string | null;
        user_agent: string | null;
        expires_at: string;
      }
    | undefined;

  if (!row) return null;

  db.prepare(`UPDATE admin_sessions SET last_seen_at = datetime('now') WHERE token_hash = ?`).run(
    tokenHash,
  );

  return {
    tokenHash,
    expiresAt: row.expires_at,
    ip: row.ip,
    userAgent: row.user_agent,
  };
}

export function createAdminSessionToken(opts: { ip: string; userAgent: string }) {
  const token = generateRandomToken(32);
  const tokenHash = sha256Hex(token);

  const ttlSeconds = config.adminSessionCookie.ttlSeconds;
  const expiresModifier = `+${ttlSeconds} seconds`;

  db.prepare(
    `INSERT INTO admin_sessions (token_hash, expires_at, ip, user_agent, last_seen_at)
     VALUES (?, datetime('now', ?), ?, ?, datetime('now'))`,
  ).run(tokenHash, expiresModifier, opts.ip, opts.userAgent);

  return token;
}

export function purgeExpiredSessions() {
  db.prepare(`DELETE FROM admin_sessions WHERE expires_at <= datetime('now')`).run();
}

export function invalidateAllSessions() {
  db.prepare(`DELETE FROM admin_sessions`).run();
}

// Periodic cleanup
setInterval(() => purgeExpiredSessions(), 60 * 60 * 1000).unref();
