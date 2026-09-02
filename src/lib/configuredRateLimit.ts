/** Renz-Fi managed HotSpot profile: renzfi-speed-{down}m-{up}m */
const MANAGED_PROFILE_RE = /^renzfi-speed-(\d+)m-(\d+)m$/i;

/** RouterOS rate-limit token pair: 50M/50M */
const ROUTEROS_RATE_LIMIT_RE = /^(\d+)M\/(\d+)M$/i;

export type ProfileRateDetail = {
  name: string;
  rateLimit?: string;
};

export function parseManagedSpeedProfileName(
  profile: string,
): { downloadMbps: number; uploadMbps: number } | null {
  const match = profile.trim().match(MANAGED_PROFILE_RE);
  if (!match) return null;
  const downloadMbps = Number(match[1]);
  const uploadMbps = Number(match[2]);
  if (!Number.isFinite(downloadMbps) || !Number.isFinite(uploadMbps)) return null;
  if (downloadMbps <= 0 || uploadMbps <= 0) return null;
  return { downloadMbps, uploadMbps };
}

export function formatConfiguredRateLimitMbps(downloadMbps: number, uploadMbps: number): string {
  return `${downloadMbps} / ${uploadMbps} Mbps`;
}

export function formatRouterOsRateLimit(rateLimit: string): string | null {
  const match = rateLimit.trim().match(ROUTEROS_RATE_LIMIT_RE);
  if (!match) return null;
  return formatConfiguredRateLimitMbps(Number(match[1]), Number(match[2]));
}

/**
 * Resolve configured HotSpot rate limit for display (not live throughput).
 * Prefers persisted sale.speed when present.
 */
export function resolveConfiguredRateLimitDisplay(
  profile: string | undefined,
  persistedSpeed: string | undefined,
  profileDetails?: ProfileRateDetail[],
): string {
  const saved = persistedSpeed?.trim();
  if (saved) return saved;

  const profileName = profile?.trim();
  if (!profileName) return "";

  const managed = parseManagedSpeedProfileName(profileName);
  if (managed) {
    return formatConfiguredRateLimitMbps(managed.downloadMbps, managed.uploadMbps);
  }

  if (profileDetails?.length) {
    const detail = profileDetails.find((row) => row.name === profileName);
    const rateLimit = detail?.rateLimit?.trim();
    if (rateLimit) {
      const formatted = formatRouterOsRateLimit(rateLimit);
      return formatted ?? rateLimit;
    }
  }

  return "";
}
