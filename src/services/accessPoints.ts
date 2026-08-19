import { api, ApiError, isApiError, isNetworkError } from "./api";
import { embeddedApi } from "./embeddedApi";

export const ACCESS_POINT_VENDORS = [
  "generic",
  "tp-link",
  "ruijie",
  "tenda",
  "other",
] as const;

export type AccessPointVendor = (typeof ACCESS_POINT_VENDORS)[number];

export type AccessPointStatus =
  | "unknown"
  | "disabled"
  | "online"
  | "network_reachable"
  | "management_reachable"
  | "auth_failed"
  | "unreachable"
  | string;

export type AccessPointCapabilities = {
  icmp?: boolean;
  http?: boolean;
  https?: boolean;
};

export type AccessPointRecord = {
  id: string;
  name: string;
  enabled: boolean;
  vendor: AccessPointVendor | string;
  model: string;
  managementIp: string;
  hasCredentials: boolean;
  ssid: string;
  location: string;
  notes: string;
  status?: AccessPointStatus | null;
  latencyMs?: number | null;
  lastCheck?: number | null;
  lastSuccessfulCheck?: number | null;
  lastError?: string | null;
  capabilities?: AccessPointCapabilities;
};

export type AccessPointList = {
  schemaVersion: number;
  accessPoints: AccessPointRecord[];
  registryError?: string;
};

export type AccessPointWritePayload = {
  name: string;
  managementIp: string;
  enabled?: boolean;
  vendor?: string;
  model?: string;
  username?: string;
  password?: string;
  ssid?: string;
  location?: string;
  notes?: string;
};

export type AccessPointCheckQueued = {
  jobId: number;
  accessPointId: string;
  state?: string;
};

export type AccessPointJob = {
  jobId: number;
  accessPointId?: string;
  state?: string;
  ok?: boolean;
  status?: AccessPointStatus;
  latencyMs?: number | null;
  startedAt?: number;
  completedAt?: number;
  errorCode?: string;
  message?: string;
};

export type AccessPointDetectQueued = {
  jobId: number;
  state?: string;
};

export type AccessPointDetectDevice = {
  ip: string;
  mac: string;
  interface?: string;
  bridgePort?: string;
  hostname?: string;
  status?: string;
};

export type AccessPointDetectResult = {
  devices: AccessPointDetectDevice[];
  source?: string;
  oneTime?: boolean;
  arpRows?: number;
  returned?: number;
  filteredOut?: number;
  bridgeHostWarning?: string;
  leaseWarning?: string;
};

export type AccessPointDetectJob = {
  jobId: number;
  state?: string;
  ok?: boolean;
  httpStatus?: number;
  result?: string;
};

const POLL_MS = 450;
const POLL_DEADLINE_MS = 30_000;
const NETWORK_RETRY_LIMIT = 4;

const activePollers = new Map<number, Promise<AccessPointJob>>();
const activeDetectPollers = new Map<number, Promise<AccessPointDetectResult>>();

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function normalizeJobState(job: AccessPointJob): string {
  return (job.state ?? "").trim().toLowerCase();
}

function isTerminalState(state: string): boolean {
  return state === "completed" || state === "failed";
}

function isRunningState(state: string): boolean {
  return state === "queued" || state === "running";
}

async function pollAccessPointJobOnce(jobId: number): Promise<AccessPointJob> {
  const startedAt = Date.now();
  let networkFailures = 0;
  let last: AccessPointJob | null = null;

  while (Date.now() - startedAt < POLL_DEADLINE_MS) {
    let job: AccessPointJob;
    try {
      job = await api.get<AccessPointJob>(
        `${embeddedApi.accessPoints}/jobs/${jobId}`,
        { timeoutMs: 15_000 },
      );
      networkFailures = 0;
    } catch (err) {
      if (isNetworkError(err) && networkFailures < NETWORK_RETRY_LIMIT) {
        networkFailures += 1;
        await sleep(POLL_MS);
        continue;
      }
      if (isApiError(err) && err.status === 404) {
        throw new Error("Access point check job was not found.");
      }
      throw err;
    }

    last = job;
    const state = normalizeJobState(job);
    if (isRunningState(state)) {
      await sleep(POLL_MS);
      continue;
    }
    if (isTerminalState(state)) {
      return job;
    }
    await sleep(POLL_MS);
  }

  throw new Error(
    last?.message ||
      "Access point check timed out waiting for job status. The check was not treated as success.",
  );
}

export const accessPointsApi = {
  list: () => api.get<AccessPointList>(embeddedApi.accessPoints),
  get: (id: string) =>
    api.get<AccessPointRecord>(`${embeddedApi.accessPoints}/${encodeURIComponent(id)}`),
  create: (payload: AccessPointWritePayload) =>
    api.post<AccessPointRecord>(embeddedApi.accessPoints, payload),
  update: (id: string, payload: AccessPointWritePayload) =>
    api.put<AccessPointRecord>(
      `${embeddedApi.accessPoints}/${encodeURIComponent(id)}`,
      payload,
    ),
  remove: (id: string) =>
    api.delete<{ ok: boolean; id: string }>(
      `${embeddedApi.accessPoints}/${encodeURIComponent(id)}`,
    ),
  checkAccessPoint: (id: string) =>
    api.post<AccessPointCheckQueued>(
      `${embeddedApi.accessPoints}/${encodeURIComponent(id)}/check`,
      {},
    ),
  getAccessPointJob: (jobId: number) =>
    api.get<AccessPointJob>(`${embeddedApi.accessPoints}/jobs/${jobId}`),
  detectAccessPoints: () =>
    api.post<AccessPointDetectQueued>(`${embeddedApi.accessPoints}/detect`, {}),
  getDetectJob: (jobId: number) =>
    api.get<AccessPointDetectJob>(`${embeddedApi.accessPoints}/detect/jobs/${jobId}`),
  checkAndWait: async (id: string): Promise<AccessPointJob> => {
    const queued = await accessPointsApi.checkAccessPoint(id);
    const jobId = queued.jobId;
    if (!jobId) {
      throw new ApiError("Check did not return a job id", 500, "CHECK_FAILED");
    }
    const existing = activePollers.get(jobId);
    if (existing) return existing;
    const poller = pollAccessPointJobOnce(jobId).finally(() => {
      activePollers.delete(jobId);
    });
    activePollers.set(jobId, poller);
    return poller;
  },
  detectAndWait: async (): Promise<AccessPointDetectResult> => {
    const queued = await accessPointsApi.detectAccessPoints();
    const jobId = queued.jobId;
    if (!jobId) {
      throw new ApiError("Detect did not return a job id", 500, "DETECT_FAILED");
    }
    const existing = activeDetectPollers.get(jobId);
    if (existing) return existing;
    const poller = (async () => {
      const startedAt = Date.now();
      while (Date.now() - startedAt < POLL_DEADLINE_MS) {
        const job = await accessPointsApi.getDetectJob(jobId);
        const state = (job.state ?? "").trim().toLowerCase();
        if (state === "queued" || state === "running") {
          await sleep(POLL_MS);
          continue;
        }
        if (state !== "completed") {
          throw new Error("Detect job failed");
        }
        if (!job.result) return { devices: [] };
        const parsed = JSON.parse(job.result) as {
          success?: boolean;
          data?: AccessPointDetectResult;
          error?: string;
        };
        if (parsed.success === false) {
          throw new Error(parsed.error || "Detect failed");
        }
        return parsed.data ?? { devices: [] };
      }
      throw new Error("Detect job timed out");
    })().finally(() => {
      activeDetectPollers.delete(jobId);
    });
    activeDetectPollers.set(jobId, poller);
    return poller;
  },
};
