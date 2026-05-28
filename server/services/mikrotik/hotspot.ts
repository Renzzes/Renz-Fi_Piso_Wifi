import { db } from "../../db/connection.js";
import type { RouterConfig } from "./types.js";

export function getRouterConfig(): RouterConfig {
  const get = (key: string, fallback = "") => {
    const row = db.prepare("SELECT value FROM router_settings WHERE key = ?").get(key) as
      | { value: string }
      | undefined;
    return row?.value ?? fallback;
  };

  return {
    host: get("host", "10.0.0.1"),
    username: get("username", "admin"),
    password: get("password", ""),
    profile: get("profile", "default"),
    ssid: get("ssid", "Renz-Fi"),
  };
}

export function saveRouterConfig(config: Partial<RouterConfig>) {
  const upsert = db.prepare(
    `INSERT INTO router_settings (key, value) VALUES (?, ?)
     ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
  );

  if (config.host !== undefined) upsert.run("host", config.host);
  if (config.username !== undefined) upsert.run("username", config.username);
  if (config.password !== undefined) upsert.run("password", config.password);
  if (config.profile !== undefined) upsert.run("profile", config.profile);
  if (config.ssid !== undefined) upsert.run("ssid", config.ssid);
}
