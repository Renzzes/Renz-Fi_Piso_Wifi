/**
 * Hotspot profile adapter boundary (stub).
 * Keeping it separate makes it easier to plug in RouterOS implementation later.
 */
export async function validateProfileName(_profile: string) {
  return true;
}
