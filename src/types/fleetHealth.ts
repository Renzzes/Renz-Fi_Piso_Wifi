import type { ApplianceBuildMetadata } from "@/types/buildMetadata";
import type { DeviceProfile, RegisteredDevice } from "@/types/deviceProfile";

/** Client-computed fleet health level — never returned by firmware. */
export type FleetHealthLevel = "healthy" | "warning" | "offline";

export const FLEET_POLL_INTERVALS_MS = [15_000, 30_000, 60_000] as const;
export type FleetPollIntervalMs = (typeof FLEET_POLL_INTERVALS_MS)[number];

export interface ApplianceStorageHealth {
  ok: boolean;
  sdMounted?: boolean;
  sdPresent?: boolean;
  fallbackActive?: boolean;
  storageMode?: string;
  spiffsReady?: boolean;
}

export interface ApplianceInstallationHealth {
  state: string;
  ready: boolean;
  needsSetup: boolean;
  progressPercent: number;
}

export interface ApplianceRouterHealth {
  configured: boolean;
  driverId: string | null;
}

export interface AppliancePortalHealth {
  revision: number;
  hasBanner: boolean;
  hasMusic: boolean;
}

export interface ApplianceCoinHealth {
  enabled: boolean;
  ok?: boolean;
  fault?: boolean;
}

/** Facts parsed from GET /api/health — firmware returns data, client interprets. */
export interface ApplianceHealthSnapshot {
  probeOk: boolean;
  fetchedAt: string;
  profile: DeviceProfile | null;
  storage: ApplianceStorageHealth;
  installation: ApplianceInstallationHealth | null;
  router: ApplianceRouterHealth | null;
  portal: AppliancePortalHealth | null;
  coin: ApplianceCoinHealth | null;
  build: ApplianceBuildMetadata | null;
  uptimeSeconds: number | null;
  serverTimeMs: number | null;
  sessionAuthenticated: boolean;
}

export interface FleetApplianceHealth {
  deviceId: string;
  device: RegisteredDevice;
  level: FleetHealthLevel;
  /** Client-computed 0–100 score (not persisted on appliance). */
  score: number;
  warnings: string[];
  snapshot: ApplianceHealthSnapshot | null;
  lastRefreshed: string | null;
}

export function fleetHealthLabel(level: FleetHealthLevel): string {
  switch (level) {
    case "healthy":
      return "Healthy";
    case "warning":
      return "Warning";
    case "offline":
      return "Offline";
  }
}

export function fleetHealthEmoji(level: FleetHealthLevel): string {
  switch (level) {
    case "healthy":
      return "🟢";
    case "warning":
      return "🟡";
    case "offline":
      return "🔴";
  }
}
