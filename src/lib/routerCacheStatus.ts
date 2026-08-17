const MIN_SANE_WALL_MS = Date.parse("2020-01-01T00:00:00Z");

export type RouterCacheStatusFields = {
  populated?: boolean;
  identity?: string;
  routerOsVersion?: string;
  lastSynchronizedAt?: string;
  cachedAt?: string;
  provisionStatus?: string;
  cacheAgeSeconds?: number;
  staleThresholdHours?: number;
  stale?: boolean;
  /** False when ESP wall clock is unsynced (pre-NTP); prefer relative age. */
  syncWallClockValid?: boolean;
  lastSynchronizedMillis?: number;
};

function isSaneIsoStamp(stamp: string): boolean {
  if (!stamp || stamp.startsWith("1970")) return false;
  const ms = Date.parse(stamp);
  return Number.isFinite(ms) && ms >= MIN_SANE_WALL_MS;
}

/** Human-readable cache age from firmware-reported seconds. */
export function formatRouterCacheAge(cacheAgeSeconds: number | undefined): string {
  if (cacheAgeSeconds === undefined) return "—";
  if (cacheAgeSeconds <= 0) return "Just now";
  if (cacheAgeSeconds < 60) return `${cacheAgeSeconds}s ago`;
  if (cacheAgeSeconds < 3600) return `${Math.floor(cacheAgeSeconds / 60)}m ago`;
  if (cacheAgeSeconds < 86400) {
    const hours = Math.floor(cacheAgeSeconds / 3600);
    const minutes = Math.floor((cacheAgeSeconds % 3600) / 60);
    return minutes > 0 ? `${hours}h ${minutes}m ago` : `${hours}h ago`;
  }
  const days = Math.floor(cacheAgeSeconds / 86400);
  const hours = Math.floor((cacheAgeSeconds % 86400) / 3600);
  return hours > 0 ? `${days}d ${hours}h ago` : `${days}d ago`;
}

export function routerCacheLastSyncLabel(
  cache: RouterCacheStatusFields | null | undefined,
): string {
  if (!cache) return "Never synchronized";
  const stamp = cache.lastSynchronizedAt?.trim() || cache.cachedAt?.trim();
  const hasSync =
    cache.populated === true ||
    cache.lastSynchronizedMillis !== undefined ||
    (stamp !== undefined && stamp.length > 0) ||
    cache.cacheAgeSeconds !== undefined;

  const wallOk =
    cache.syncWallClockValid !== false && stamp && isSaneIsoStamp(stamp);
  if (wallOk) return stamp!;

  if (
    hasSync &&
    cache.cacheAgeSeconds !== undefined &&
    (cache.populated === true ||
      cache.lastSynchronizedMillis !== undefined ||
      cache.syncWallClockValid === false)
  ) {
    return formatRouterCacheAge(cache.cacheAgeSeconds);
  }

  if (stamp && !isSaneIsoStamp(stamp)) {
    return "Clock not synchronized";
  }
  if (!stamp) return "Never synchronized";
  return stamp;
}

export function isRouterCacheStale(
  cache: RouterCacheStatusFields | null | undefined,
): boolean {
  return Boolean(cache?.populated && cache?.stale);
}

/**
 * Display-only Provision Status. Finish owns backend `provisionStatus=provisioned`
 * and `productionNetwork`; Admin Sync intentionally does not set those.
 * When Sync has populated identity/SSID, show Synchronized instead of a blank.
 */
export function routerCacheProvisionStatusLabel(
  cache: RouterCacheStatusFields & {
    ssid?: string;
    productionNetwork?: { verified?: boolean; reason?: string } | null;
  } | null | undefined,
): string {
  const explicit = cache?.provisionStatus?.trim();
  if (explicit) return explicit;
  if (cache?.ssid?.trim() || cache?.identity?.trim()) return "Synchronized";
  return "—";
}

/** Display-only Production Wi-Fi row when Finish verification object is absent. */
export function routerCacheProductionWifiLabel(
  cache: {
    ssid?: string;
    productionNetwork?: { verified?: boolean; reason?: string } | null;
  } | null | undefined,
  healthyLabel: string,
  unhealthyReasonLabel: string,
): string {
  const pn = cache?.productionNetwork;
  if (pn) {
    return pn.verified ? healthyLabel : unhealthyReasonLabel;
  }
  if (cache?.ssid?.trim()) return "SSID synchronized";
  return "—";
}
