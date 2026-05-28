import { db } from "../../db/connection.js";

export async function disconnectUser(mac: string): Promise<boolean> {
  const result = db.prepare("DELETE FROM active_sessions WHERE mac = ?").run(mac);
  if (result.changes > 0) {
    db.prepare(`INSERT INTO logs (level, message) VALUES ('INFO', ?)`).run(
      `Disconnected user ${mac} via MikroTik adapter`,
    );
  }
  return result.changes > 0;
}
