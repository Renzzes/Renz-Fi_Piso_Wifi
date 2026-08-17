import type { RouterConfig, RouterWireless } from "@/services/router";

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
      username: "",
      password: "",
      profile: "default",
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
    username: raw.username?.trim() || "",
    password: "",
    profile: raw.profile?.trim() || "default",
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
  };

  if (config.password.trim().length > 0) {
    payload.password = config.password;
  }

  return payload;
}

/** Payload for POST /api/router/test — uses entered password or leaves blank for stored fallback. */
export function toRouterTestPayload(config: RouterConfig): Partial<RouterConfig> {
  return toRouterSavePayload(config);
}

/** Cached wireless form state — sourced from ESP32 router cache (GET /api/router/wireless). */
export type RouterWirelessForm = {
  ssid: string;
  password: string;
  security: string;
};

export function normalizeRouterWireless(
  raw: RouterWireless | null | undefined,
): RouterWirelessForm {
  const securityRaw = raw?.security?.trim() ?? "";
  return {
    ssid: raw?.ssid?.trim() ?? "",
    password: raw?.password ?? "",
    security: securityRaw,
  };
}

/** Display label for wireless security — never invent Open from missing data. */
export function formatWirelessSecurityLabel(security?: string | null): string {
  const raw = (security ?? "").trim();
  if (!raw) return "Unknown";
  const lower = raw.toLowerCase();
  if (lower === "unknown") return "Unknown";
  if (lower === "none" || lower === "open") return "Open";
  return raw;
}

export function isOpenWirelessSecurity(security?: string | null): boolean {
  const lower = (security ?? "").trim().toLowerCase();
  return lower === "none" || lower === "open";
}

export function wirelessSourceLabel(wifiMode?: string): string {
  if (wifiMode === "new") return "Renz-Fi Access Point";
  if (wifiMode === "existing") return "Existing Router SSID";
  return "—";
}

export function formatWirelessBand(
  band?: string,
  frequencyMhz?: number | string | null,
): string {
  if (band) {
    if (band === "2.4GHz") return "2.4 GHz";
    if (band === "5GHz") return "5 GHz";
    return band;
  }
  if (frequencyMhz == null || frequencyMhz === "") return "—";
  const mhz = typeof frequencyMhz === "string" ? Number.parseInt(frequencyMhz, 10) : frequencyMhz;
  if (!Number.isFinite(mhz) || mhz <= 0) return "—";
  return mhz >= 5000 ? "5 GHz" : "2.4 GHz";
}

export function wirelessConfigurationStatus(configured?: boolean): string {
  return configured ? "Configured" : "Not configured";
}
