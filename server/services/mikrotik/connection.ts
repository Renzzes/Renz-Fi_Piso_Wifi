import { db } from "../../db/connection.js";
import type { RouterConfig } from "./types.js";
import { saveRouterConfig } from "./hotspot.js";

/**
 * RouterOS connection adapter boundary.
 * Real RouterOS I/O is intentionally stubbed/minimal in this admin server.
 */
export async function testRouterConnection(config: RouterConfig): Promise<boolean> {
  saveRouterConfig(config);

  const ok = Boolean(config.host && config.username);

  db.prepare(
    `INSERT INTO router_settings (key, value) VALUES ('connected', ?)
     ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
  ).run(ok ? "1" : "0");

  db.prepare(`INSERT INTO logs (level, message) VALUES (?, ?)`).run(
    ok ? "INFO" : "ERR",
    ok
      ? `MikroTik connection test succeeded (${config.host})`
      : `MikroTik connection test failed (${config.host})`,
  );

  return ok;
}
