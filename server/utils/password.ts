import bcrypt from "bcrypt";

const BCRYPT_ROUNDS = 10;

export function isBcryptHash(value: string) {
  return /^\$2[aby]?\$\d{2}\$/.test(value);
}

export async function hashPassword(plain: string) {
  return bcrypt.hash(plain, BCRYPT_ROUNDS);
}

export async function verifyPassword(plain: string, stored: string) {
  if (!stored) return false;
  if (isBcryptHash(stored)) {
    return bcrypt.compare(plain, stored);
  }
  // Legacy plain-text password from admin_settings (migrate on successful login).
  return plain === stored;
}
