import { settingsRepository } from "../db/repositories/settingsRepository.js";
import { adminAuthRepository } from "../db/repositories/adminAuthRepository.js";
import { hashPassword, isBcryptHash, verifyPassword } from "../utils/password.js";
import { config } from "../config/index.js";
import { logStructured } from "./logger.js";

export async function verifyAdminPassword(password: string) {
  if (adminAuthRepository.isLocked()) {
    return { ok: false as const, code: "LOCKED" as const };
  }

  const stored = settingsRepository.getPasswordHash();
  if (!stored) {
    return { ok: false as const, code: "NO_PASSWORD" as const };
  }

  const valid = await verifyPassword(password, stored);
  if (!valid) {
    adminAuthRepository.recordFailedAttempt(
      config.auth.maxFailedAttempts,
      config.auth.lockoutMinutes,
    );
    logStructured({
      level: "WARN",
      category: "auth",
      message: "Invalid admin login attempt",
      metadata: { failedAttempts: adminAuthRepository.getFailedAttempts() },
    });
    return { ok: false as const, code: "INVALID" as const };
  }

  if (!isBcryptHash(stored)) {
    const hashed = await hashPassword(password);
    settingsRepository.setPasswordHash(hashed);
  }

  adminAuthRepository.resetFailedAttempts();
  return { ok: true as const };
}

export function getAdminAuthStats() {
  return {
    failedAttempts: adminAuthRepository.getFailedAttempts(),
    locked: adminAuthRepository.isLocked(),
  };
}
