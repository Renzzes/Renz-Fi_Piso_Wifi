import { getRouterConfig } from "./hotspot.js";

export type HotspotProfilesResult = {
  profiles: string[];
  error?: string;
};

/**
 * Dev-server profile list boundary.
 * Production ESP32 firmware queries RouterOS directly.
 */
export function listHotspotProfiles(): HotspotProfilesResult {
  const config = getRouterConfig();
  if (!config.host?.trim()) {
    return { profiles: [], error: "MikroTik Router IP is not configured" };
  }
  if (!config.username?.trim()) {
    return { profiles: [], error: "RouterOS API username is not configured" };
  }
  if (!config.password) {
    return { profiles: [], error: "RouterOS API password is not configured" };
  }

  return {
    profiles: config.profile ? [config.profile] : [],
    error: "RouterOS profile discovery is only available on embedded firmware",
  };
}

export async function validateProfileName(_profile: string) {
  return true;
}
