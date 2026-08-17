import { api, ApiError, apiFetch } from "./api";
import { embeddedApi } from "./embeddedApi";

/** RouterOS API connection details only — never wireless/SSID (see RouterWireless). */
export type RouterConfig = {
  host: string;
  username: string;
  password: string;
  profile: string;
  passwordConfigured?: boolean;
};

/** Wireless interface state — written to RouterOS; reads use provisioning file + live RouterOS. */
export type RouterWireless = {
  ssid: string;
  security: string;
  password?: string;
  interface?: string;
  interfaceId?: string;
  wifiMode?: string;
  configured?: boolean;
  band?: string;
  truncated?: boolean;
  cached?: boolean;
  error?: string;
  applied?: boolean;
  verified?: boolean;
  verification?: string;
};

export type RouterTestStep = {
  id: "api_reachable" | "login" | "profile";
  label: string;
  ok: boolean;
  message: string;
};

export type RouterOsResourceSnapshot = {
  version?: string;
  cpuLoad?: string;
  freeMemory?: string;
  totalMemory?: string;
  uptime?: string;
};

export type ProductionNetworkCacheSnapshot = {
  verified?: boolean;
  interface?: string;
  ssid?: string;
  expectedSsid?: string;
  frequency?: number | string;
  channel?: number | string;
  mode?: string;
  verifiedAt?: string;
  reason?: string;
};

export type RouterHotspotUserProfile = {
  name: string;
  rateLimit?: string;
};

export type RouterOsTestResult = {
  ok: boolean;
  connected?: boolean;
  authenticated?: boolean;
  profileFound?: boolean;
  identity?: string;
  error?: string;
  routerOs?: RouterOsResourceSnapshot;
  profiles?: string[];
  profileDetails?: RouterHotspotUserProfile[];
  truncated?: boolean;
};

export type RouterTestResult = RouterOsTestResult & {
  steps?: RouterTestStep[];
  summary?: string;
};

export type RouterProfilesResult = {
  profiles: string[];
  profileDetails?: RouterHotspotUserProfile[];
  truncated?: boolean;
  cached?: boolean;
  stale?: boolean;
  lastSynchronizedAt?: string;
  error?: string;
};

export type RouterCacheSnapshot = {
  populated?: boolean;
  routerIp?: string;
  identity?: string;
  routerOsVersion?: string;
  wirelessInterface?: string;
  ssid?: string;
  security?: string;
  bridge?: string;
  hotspotServer?: string;
  hotspotProfile?: string;
  htmlDirectory?: string;
  provisionTimestamp?: string;
  provisionStatus?: string;
  lastSynchronizedAt?: string;
  cachedAt?: string;
  cacheAgeSeconds?: number;
  staleThresholdHours?: number;
  stale?: boolean;
  profiles?: string[];
  profileDetails?: RouterHotspotUserProfile[];
  routerOs?: RouterOsResourceSnapshot;
  productionNetwork?: ProductionNetworkCacheSnapshot;
  observation?: {
    connectivity?: string;
    hotspotStatus?: string;
    lastSuccessfulContactAt?: string;
    lastContactError?: string;
  };
  error?: string;
};

type RouterJobQueued = {
  jobId: number;
  state: string;
  type?: string;
};

type RouterJobPoll = {
  jobId: number;
  state: string;
  httpStatus?: number;
  result?: unknown;
  stage?: string;
  label?: string;
};

const ADMIN_JOB_POLL_MS = 1000;
const ADMIN_JOB_MAX_POLLS = 90; // 90s ceiling — worker job timeout is 20s + headroom

function normalizeProfileDetails(raw: unknown): RouterHotspotUserProfile[] {
  if (!Array.isArray(raw)) return [];
  const details: RouterHotspotUserProfile[] = [];
  for (const item of raw) {
    if (typeof item === "string" && item.length > 0) {
      details.push({ name: item, rateLimit: "" });
      continue;
    }
    if (!item || typeof item !== "object") continue;
    const record = item as Record<string, unknown>;
    const name =
      typeof record.name === "string"
        ? record.name
        : typeof record.Name === "string"
          ? record.Name
          : "";
    if (!name) continue;
    const rateLimit =
      typeof record.rateLimit === "string"
        ? record.rateLimit
        : typeof record["rate-limit"] === "string"
          ? record["rate-limit"]
          : "";
    details.push({ name, rateLimit });
  }
  return details;
}

function normalizeProfilesResult(raw: unknown): RouterProfilesResult {
  if (!raw || typeof raw !== "object") {
    return { profiles: [], profileDetails: [], error: "Invalid profiles response" };
  }
  const record = raw as Record<string, unknown>;
  if (record.data && typeof record.data === "object" && !Array.isArray(record.profiles)) {
    return normalizeProfilesResult(record.data);
  }

  const profilesFromNames = Array.isArray(record.profiles)
    ? record.profiles.filter((p): p is string => typeof p === "string")
    : [];
  let profileDetails = normalizeProfileDetails(record.profileDetails);
  if (profileDetails.length === 0 && Array.isArray(record.profiles)) {
    profileDetails = normalizeProfileDetails(record.profiles);
  }
  const profiles =
    profilesFromNames.length > 0
      ? profilesFromNames
      : profileDetails.map((p) => p.name);

  return {
    profiles,
    profileDetails:
      profileDetails.length > 0
        ? profileDetails
        : profiles.map((name) => ({ name, rateLimit: "" })),
    truncated: record.truncated === true,
    cached: record.cached === true,
    stale: record.stale === true,
    lastSynchronizedAt:
      typeof record.lastSynchronizedAt === "string" ? record.lastSynchronizedAt : undefined,
    error:
      typeof record.error === "string" && record.error.length > 0 ? record.error : undefined,
  };
}

async function coerceJobResultPayload(raw: unknown): Promise<unknown> {
  if (typeof raw === "string") {
    const trimmed = raw.trim();
    if (!trimmed) return undefined;
    try {
      return JSON.parse(trimmed) as unknown;
    } catch {
      return raw;
    }
  }
  return raw;
}

async function pollAdminRouterJob<T>(jobId: number): Promise<T> {
  for (let i = 0; i < ADMIN_JOB_MAX_POLLS; i++) {
    const poll = await api.get<RouterJobPoll>(`${embeddedApi.router}/jobs/${jobId}`);
    const state = poll.state;
    if (state === "queued" || state === "running") {
      await new Promise((r) => setTimeout(r, ADMIN_JOB_POLL_MS));
      continue;
    }

    const httpStatus = poll.httpStatus ?? 500;
    const raw = await coerceJobResultPayload(poll.result);
    if (raw && typeof raw === "object" && "success" in raw) {
      const envelope = raw as {
        success: boolean;
        data?: T;
        error?: unknown;
        code?: string;
        message?: string;
      };
      if (envelope.success) {
        // Job completed with success envelope — never treat missing data as a
        // soft failure (that resurrects stale "API unreachable" banners).
        return (envelope.data ?? ({} as T)) as T;
      }
      throw new ApiError(
        String(envelope.error ?? envelope.message ?? "Router job failed"),
        httpStatus,
        envelope.code,
      );
    }

    if (state === "completed") {
      return (raw ?? ({} as T)) as T;
    }
    throw new ApiError(
      typeof raw === "string" && raw.length > 0 ? raw : "Router job failed",
      httpStatus,
      "ROUTER_JOB_FAILED",
    );
  }
  throw new ApiError("Router job timed out", 504, "ROUTER_JOB_TIMEOUT");
}

async function enqueueAdminRouterJob<T>(
  path: string,
  init?: RequestInit,
): Promise<T> {
  const queued = await apiFetch<RouterJobQueued>(path, init);
  if (!queued?.jobId) {
    throw new ApiError("Router job id missing from response", 502, "ROUTER_JOB_ID_MISSING");
  }
  return pollAdminRouterJob<T>(queued.jobId);
}

export const routerApi = {
  settings: () => api.get<RouterConfig>(`${embeddedApi.router}/settings`),
  save: (config: Partial<RouterConfig>) =>
    enqueueAdminRouterJob<{ ok: boolean }>(`${embeddedApi.router}/settings`, {
      method: "PUT",
      body: JSON.stringify(config),
    }),
  test: (config?: Partial<RouterConfig>) =>
    enqueueAdminRouterJob<RouterTestResult>(`${embeddedApi.router}/test`, {
      method: "POST",
      body: JSON.stringify(config ?? {}),
    }),

  profiles: async () => {
    const response = await api.get<RouterProfilesResult>(`${embeddedApi.router}/profiles`);
    return normalizeProfilesResult(response);
  },

  refreshProfiles: () =>
    enqueueAdminRouterJob<RouterProfilesResult>(`${embeddedApi.router}/profiles/refresh`, {
      method: "POST",
      body: "{}",
    }),

  profileOp: (body: Record<string, unknown>) =>
    enqueueAdminRouterJob<Record<string, unknown>>(`${embeddedApi.router}/profiles/op`, {
      method: "POST",
      body: JSON.stringify(body),
    }),

  wireless: () => api.get<RouterWireless>(`${embeddedApi.router}/wireless`),
  saveWireless: (settings: { ssid: string; password?: string }) =>
    enqueueAdminRouterJob<RouterWireless>(`${embeddedApi.router}/wireless`, {
      method: "PUT",
      body: JSON.stringify(settings),
    }),

  cache: () => api.get<RouterCacheSnapshot>(`${embeddedApi.router}/cache`),
  refreshCache: () =>
    enqueueAdminRouterJob<RouterCacheSnapshot>(`${embeddedApi.router}/cache/refresh`, {
      method: "POST",
      body: "{}",
    }),
  syncRouter: () =>
    enqueueAdminRouterJob<RouterCacheSnapshot>(`${embeddedApi.router}/cache/sync`, {
      method: "POST",
      body: "{}",
    }),
};
