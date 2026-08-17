import { parseApplianceBuildMetadata } from "@/types/buildMetadata";
import { deviceProfileFromHealth, type RegisteredDevice } from "@/types/deviceProfile";
import type {
  ApplianceCoinHealth,
  ApplianceHealthSnapshot,
  ApplianceInstallationHealth,
  AppliancePortalHealth,
  ApplianceRouterHealth,
  ApplianceStorageHealth,
  FleetApplianceHealth,
  FleetHealthLevel,
} from "@/types/fleetHealth";
import { embeddedApi } from "@/services/embeddedApi";

const HEALTH_PROBE_TIMEOUT_MS = 5_000;

function parseStorage(raw: Record<string, unknown> | undefined): ApplianceStorageHealth {
  if (!raw) return { ok: false };
  return {
    ok: raw.ok !== false,
    sdMounted: raw.sdMounted === true,
    sdPresent: raw.sdPresent === true,
    fallbackActive: raw.fallbackActive === true,
    storageMode: typeof raw.storageMode === "string" ? raw.storageMode : undefined,
    spiffsReady: raw.spiffsReady === true,
  };
}

function parseInstallation(
  raw: Record<string, unknown> | undefined,
): ApplianceInstallationHealth | null {
  if (!raw) return null;
  return {
    state: String(raw.state ?? "factory"),
    ready: raw.ready === true,
    needsSetup: raw.needsSetup === true,
    progressPercent: typeof raw.progressPercent === "number" ? raw.progressPercent : 0,
  };
}

function parseRouter(raw: Record<string, unknown> | undefined): ApplianceRouterHealth | null {
  if (!raw) return null;
  return {
    configured: raw.configured === true,
    driverId:
      raw.driverId != null && String(raw.driverId).length > 0
        ? String(raw.driverId)
        : null,
  };
}

function parsePortal(raw: Record<string, unknown> | undefined): AppliancePortalHealth | null {
  if (!raw) return null;
  return {
    revision: typeof raw.revision === "number" ? raw.revision : 0,
    hasBanner: raw.hasBanner === true,
    hasMusic: raw.hasMusic === true,
  };
}

function parseCoin(raw: Record<string, unknown> | undefined): ApplianceCoinHealth | null {
  if (!raw) return null;
  const state = String(raw.state ?? raw.hardwareState ?? "");
  return {
    enabled: raw.enabled === true,
    ok: raw.enabled === true && state !== "fault",
    fault: state === "fault" || raw.faultReason != null,
  };
}

export function parseApplianceHealthSnapshot(
  data: Record<string, unknown>,
  probeOk: boolean,
): ApplianceHealthSnapshot {
  const profile = deviceProfileFromHealth(data);
  const session = data.session as Record<string, unknown> | undefined;

  return {
    probeOk,
    fetchedAt: new Date().toISOString(),
    profile,
    storage: parseStorage(data.storage as Record<string, unknown> | undefined),
    installation: parseInstallation(data.installation as Record<string, unknown> | undefined),
    router: parseRouter(data.router as Record<string, unknown> | undefined),
    portal: parsePortal(data.portal as Record<string, unknown> | undefined),
    coin: parseCoin(data.coin as Record<string, unknown> | undefined),
    build: parseApplianceBuildMetadata(data.build),
    uptimeSeconds:
      typeof data.uptimeSeconds === "number" ? data.uptimeSeconds : null,
    serverTimeMs: typeof data.serverTimeMs === "number" ? data.serverTimeMs : null,
    sessionAuthenticated: session?.authenticated === true,
  };
}

export async function fetchApplianceHealthSnapshot(
  ip: string,
): Promise<ApplianceHealthSnapshot> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), HEALTH_PROBE_TIMEOUT_MS);
  const url = `http://${ip}${embeddedApi.health}`;

  try {
    const res = await fetch(url, { credentials: "omit", signal: controller.signal });
    if (!res.ok) {
      return {
        probeOk: false,
        fetchedAt: new Date().toISOString(),
        profile: null,
        storage: { ok: false },
        installation: null,
        router: null,
        portal: null,
        coin: null,
        build: null,
        uptimeSeconds: null,
        serverTimeMs: null,
        sessionAuthenticated: false,
      };
    }
    const json = (await res.json()) as Record<string, unknown>;
    const data = (json.data as Record<string, unknown> | undefined) ?? json;
    return parseApplianceHealthSnapshot(data, data.ok !== false);
  } catch {
    return {
      probeOk: false,
      fetchedAt: new Date().toISOString(),
      profile: null,
      storage: { ok: false },
      installation: null,
      router: null,
      portal: null,
      coin: null,
      build: null,
      uptimeSeconds: null,
      serverTimeMs: null,
      sessionAuthenticated: false,
    };
  } finally {
    clearTimeout(timer);
  }
}

/** Pure client-side health rules — firmware returns facts only. */
export function computeFleetHealth(
  device: RegisteredDevice,
  snapshot: ApplianceHealthSnapshot | null,
): Pick<FleetApplianceHealth, "level" | "score" | "warnings"> {
  const warnings: string[] = [];

  if (!snapshot?.probeOk || !device.isOnline || !snapshot.profile?.online) {
    return { level: "offline", score: 0, warnings: ["Appliance unreachable"] };
  }

  const { storage, installation, router, portal, coin } = snapshot;

  if (!storage.ok) warnings.push("Storage degraded");
  if (storage.fallbackActive) warnings.push("SD fallback active (SPIFFS)");
  if (storage.spiffsReady === false) warnings.push("SPIFFS not ready");

  if (installation?.needsSetup && !installation.ready) {
    warnings.push(`Setup incomplete (${installation.state})`);
  }

  if (router && snapshot.profile?.routerDriver && !router.configured) {
    warnings.push("Router not configured");
  }

  if (portal && installation?.ready && portal.revision === 0 && !portal.hasBanner) {
    warnings.push("Portal assets incomplete");
  }

  if (coin?.enabled && coin.fault) warnings.push("Coin hardware fault");
  if (coin?.enabled && coin.ok === false) warnings.push("Coin hardware not ready");

  if (warnings.length > 0) {
    const score = Math.max(35, 100 - warnings.length * 15);
    return { level: "warning", score, warnings };
  }

  return { level: "healthy", score: 100, warnings };
}

export function buildFleetApplianceHealth(
  device: RegisteredDevice,
  snapshot: ApplianceHealthSnapshot | null,
): FleetApplianceHealth {
  const computed = computeFleetHealth(device, snapshot);
  return {
    deviceId: device.deviceId,
    device,
    level: computed.level,
    score: computed.score,
    warnings: computed.warnings,
    snapshot,
    lastRefreshed: snapshot?.fetchedAt ?? null,
  };
}

export function isFleetHealthLevel(value: string): value is FleetHealthLevel {
  return value === "healthy" || value === "warning" || value === "offline";
}
