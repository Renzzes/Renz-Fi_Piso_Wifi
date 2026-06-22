import type { RouterConfig } from "@/services/router";

/** Production VLAN40 topology defaults (W5500Config.h). */
export const DEFAULT_ESP32_IP = "10.40.0.2";
export const DEFAULT_MIKROTIK_IP = "10.40.0.1";

type RawRouterConfig = Partial<RouterConfig> & {
  router?: { ip?: string };
  mikrotik?: { ip?: string };
};

/**
 * Normalize router settings from API / backup payloads.
 * RouterOS API password is never loaded into the form — only passwordConfigured is kept.
 */
export function normalizeRouterConfig(raw: RawRouterConfig | null | undefined): RouterConfig {
  if (!raw || typeof raw !== "object") {
    return {
      host: DEFAULT_MIKROTIK_IP,
      username: "admin",
      password: "",
      profile: "default",
      ssid: "RenzFi_PesoWifi",
      wifiPassword: "",
      passwordConfigured: false,
    };
  }

  let host =
    raw.host?.trim() ||
    raw.mikrotik?.ip?.trim() ||
    raw.router?.ip?.trim() ||
    "";

  if (host === DEFAULT_ESP32_IP) {
    host = DEFAULT_MIKROTIK_IP;
  }

  if (!host) {
    host = DEFAULT_MIKROTIK_IP;
  }

  return {
    host,
    username: raw.username?.trim() || "admin",
    password: "",
    profile: raw.profile?.trim() || "default",
    ssid: raw.ssid?.trim() || "RenzFi_PesoWifi",
    wifiPassword: raw.wifiPassword ?? "",
    passwordConfigured: Boolean(raw.passwordConfigured),
  };
}

/** True when legacy keys were present and remapped. */
export function routerConfigNeedsMigration(raw: RawRouterConfig | null | undefined): boolean {
  if (!raw || typeof raw !== "object") return false;
  const legacyHost = raw.mikrotik?.ip || raw.router?.ip;
  const wrongTopology = raw.host?.trim() === DEFAULT_ESP32_IP;
  return Boolean(legacyHost || wrongTopology);
}

/** Payload sent to PUT /api/router/settings — omit empty secrets to preserve stored values. */
export function toRouterSavePayload(config: RouterConfig): Partial<RouterConfig> {
  const payload: Partial<RouterConfig> = {
    host: config.host,
    username: config.username,
    profile: config.profile,
    ssid: config.ssid,
  };

  if (config.password.trim().length > 0) {
    payload.password = config.password;
  }

  if ((config.wifiPassword ?? "").trim().length > 0) {
    payload.wifiPassword = config.wifiPassword;
  }

  return payload;
}

/** Payload for POST /api/router/test — uses entered password or leaves blank for stored fallback. */
export function toRouterTestPayload(config: RouterConfig): Partial<RouterConfig> {
  return toRouterSavePayload(config);
}
