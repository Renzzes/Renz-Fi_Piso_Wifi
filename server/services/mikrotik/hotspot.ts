import { db } from "../../db/connection.js";
import type { RouterConfig, RouterPublicConfig } from "./types.js";

function readStored(key: string, fallback = "") {
  const row = db.prepare("SELECT value FROM router_settings WHERE key = ?").get(key) as
    | { value: string }
    | undefined;
  return row?.value ?? fallback;
}

export function getRouterConfig(): RouterConfig {
  let host = readStored("host", "10.40.0.1");
  if (host === "10.40.0.2") host = "10.40.0.1";

  return {
    host,
    username: readStored("username", "admin"),
    password: readStored("password", ""),
    profile: readStored("profile", "default"),
    ssid: readStored("ssid", "RenzFi_PesoWifi"),
    wifiPassword: readStored("wifi_password", ""),
  };
}

export function getPublicRouterConfig(): RouterPublicConfig {
  const stored = getRouterConfig();
  return {
    host: stored.host,
    username: stored.username,
    profile: stored.profile,
    ssid: stored.ssid,
    wifiPassword: stored.wifiPassword ?? "",
    passwordConfigured: stored.password.length > 0,
  };
}

export function saveRouterConfig(config: Partial<RouterConfig>) {
  const upsert = db.prepare(
    `INSERT INTO router_settings (key, value) VALUES (?, ?)
     ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
  );

  if (config.host !== undefined) upsert.run("host", config.host);
  if (config.username !== undefined) upsert.run("username", config.username);
  if (config.profile !== undefined) upsert.run("profile", config.profile);
  if (config.ssid !== undefined) upsert.run("ssid", config.ssid);

  if (config.password !== undefined && config.password.length > 0) {
    upsert.run("password", config.password);
  }

  if (config.wifiPassword !== undefined && config.wifiPassword.length > 0) {
    upsert.run("wifi_password", config.wifiPassword);
  }
}

export function resolveRouterCredentials(config: Partial<RouterConfig>): RouterConfig {
  const stored = getRouterConfig();
  return {
    host: config.host?.trim() || stored.host,
    username: config.username?.trim() || stored.username,
    password: config.password && config.password.length > 0 ? config.password : stored.password,
    profile: config.profile?.trim() || stored.profile,
    ssid: config.ssid?.trim() || stored.ssid,
    wifiPassword:
      config.wifiPassword && config.wifiPassword.length > 0
        ? config.wifiPassword
        : stored.wifiPassword,
  };
}
