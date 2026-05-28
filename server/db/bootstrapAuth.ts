import { settingsRepository } from "./repositories/settingsRepository.js";
import { hashPassword } from "../utils/password.js";

/** First-boot: ensure a local admin password exists (default: admin — change in settings). */
export async function bootstrapAdminPassword() {
  const existing = settingsRepository.getPasswordHash();
  if (!existing) {
    const hashed = await hashPassword("admin");
    settingsRepository.setPasswordHash(hashed);
  }
}
