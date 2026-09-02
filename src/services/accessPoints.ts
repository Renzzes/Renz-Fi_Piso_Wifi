import { api, ApiError, isApiError, isNetworkError } from "./api";
import { embeddedApi } from "./embeddedApi";

/** Shared React Query key — Dashboard and Access Points page must use the same cache. */
export const ACCESS_POINTS_QUERY_KEY = ["access-points"] as const;

export const ACCESS_POINT_VENDORS = ["generic", "tp-link", "ruijie", "tenda", "other"] as const;

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
  online?: boolean;
  method?: string;
  ipAddress?: string;
  managementIp?: string;
  status?: AccessPointStatus;
  latencyMs?: number | null;
  startedAt?: number;
  completedAt?: number;
  errorCode?: string;
  message?: string;
};

export type AccessPointDetectDevice = {
  ip: string;
  mac: string;
  interface?: string;
  bridgePort?: string;
  hostname?: string;
  status?: string;
};

export type AccessPointDetectPayload = {
  success?: boolean;
  message?: string;
  data?: {
    devices?: AccessPointDetectDevice[];
    registeredDevices?: AccessPointDetectDevice[];
    source?: string;
    oneTime?: boolean;
    arpRows?: number;
    returned?: number;
    filteredOut?: number;
    bridgeHostWarning?: string;
    leaseWarning?: string;
  };
};

export type AccessPointDetectQueued = {
  jobId: number;
  state?: string;
};

export type AccessPointDetectJob = {
  jobId: number;
  state?: string;
  ok?: boolean;
  result?: AccessPointDetectPayload | string;
  httpStatus?: number;
};

const POLL_MS = 450;
const POLL_DEADLINE_MS = 30_000;
const NETWORK_RETRY_LIMIT = 4;

const activePollers = new Map<number, Promise<AccessPointJob>>();
const activeDetectPollers = new Map<number, Promise<AccessPointDetectJob>>();

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

export function accessPointJobSucceeded(job: AccessPointJob): boolean {
  if (job.state === "failed") return false;
  if (job.ok === false) return false;
  if (job.online === true) return true;
  if (job.online === false) return false;
  const status = (job.status ?? "").trim().toLowerCase();
  return status === "online" || status === "network_reachable" || status === "management_reachable";
}

function parseIpv4Octets(ip: string): number[] | null {
  const parts = ip.trim().split(".");
  if (parts.length !== 4) return null;
  const octets: number[] = [];
  for (const part of parts) {
    if (!/^\d{1,3}$/.test(part)) return null;
    const n = Number(part);
    if (!Number.isInteger(n) || n < 0 || n > 255) return null;
    octets.push(n);
  }
  return octets;
}

/** Keep Detect candidates that look like private LAN hosts (not gateways/DNS). */
export function isDetectCandidateIp(ip: string): boolean {
  const o = parseIpv4Octets(ip);
  if (!o) return false;
  const [a, b, c, d] = o;
  if (d === 0 || d === 255) return false;
  // ESP32 SoftAP / appliance management exclusions
  if (a === 192 && b === 168 && c === 4) return false;
  if (ip === "10.10.10.1" || ip === "10.10.10.2" || ip === "10.20.0.1") return false;
  const privateRfc1918 = a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168);
  return privateRfc1918;
}

export function parseDetectFailureMessage(job: AccessPointDetectJob): string {
  let payload: unknown = job.result;
  if (typeof payload === "string") {
    try {
      payload = JSON.parse(payload);
    } catch {
      payload = null;
    }
  }
  if (payload && typeof payload === "object") {
    const row = payload as {
      error?: string;
      code?: string;
      message?: string;
      data?: { error?: string; code?: string } | string;
    };
    const nested =
      typeof row.data === "string"
        ? (() => {
            try {
              return JSON.parse(row.data) as {
                error?: string;
                code?: string;
              };
            } catch {
              return null;
            }
          })()
        : row.data && typeof row.data === "object"
          ? row.data
          : null;
    const error =
      (typeof row.error === "string" && row.error) ||
      (typeof nested?.error === "string" && nested.error) ||
      (typeof row.message === "string" && row.message) ||
      "";
    const code =
      (typeof row.code === "string" && row.code) ||
      (typeof nested?.code === "string" && nested.code) ||
      "";
    if (error && code) return `${error} (${code})`;
    if (error) return error;
    if (code) return code;
  }
  if (typeof job.httpStatus === "number" && job.httpStatus >= 400) {
    return `Detect failed (HTTP ${job.httpStatus})`;
  }
  return "Detect failed — confirm MikroTik credentials and Router Worker are healthy.";
}

export function parseDetectDevices(job: AccessPointDetectJob): AccessPointDetectDevice[] {
  return parseDetectDeviceRows(job, "devices");
}

/** Bridge ports for already-registered AP IPs (filtered out of candidate list). */
export function parseDetectRegisteredDevices(job: AccessPointDetectJob): AccessPointDetectDevice[] {
  return parseDetectDeviceRows(job, "registeredDevices");
}

function parseDetectDeviceRows(
  job: AccessPointDetectJob,
  key: "devices" | "registeredDevices",
): AccessPointDetectDevice[] {
  let payload = job.result;
  if (typeof payload === "string") {
    try {
      payload = JSON.parse(payload) as AccessPointDetectPayload;
    } catch {
      return [];
    }
  }
  if (!payload || typeof payload !== "object") return [];
  const devices = payload.data?.[key];
  if (!Array.isArray(devices)) return [];
  return devices.filter(
    (row): row is AccessPointDetectDevice =>
      typeof row?.ip === "string" &&
      typeof row?.mac === "string" &&
      row.ip.length > 0 &&
      row.mac.length > 0 &&
      (key === "registeredDevices" || isDetectCandidateIp(row.ip)),
  );
}

const DETECT_BRIDGE_PORT_KEY = "renzfi-ap-bridge-ports";

export function loadPersistedDetectBridgePorts(): Record<string, string> {
  if (typeof window === "undefined") return {};
  try {
    const raw = sessionStorage.getItem(DETECT_BRIDGE_PORT_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw) as unknown;
    if (!parsed || typeof parsed !== "object") return {};
    const out: Record<string, string> = {};
    for (const [ip, port] of Object.entries(parsed as Record<string, unknown>)) {
      if (typeof ip === "string" && typeof port === "string" && port.trim()) {
        out[ip.trim()] = port.trim();
      }
    }
    return out;
  } catch {
    return {};
  }
}

export function persistDetectBridgePorts(
  devices: AccessPointDetectDevice[],
): Record<string, string> {
  const existing = loadPersistedDetectBridgePorts();
  for (const device of devices) {
    const ip = device.ip?.trim();
    const port = device.bridgePort?.trim();
    if (ip && port) existing[ip] = port;
  }
  if (typeof window !== "undefined") {
    sessionStorage.setItem(DETECT_BRIDGE_PORT_KEY, JSON.stringify(existing));
  }
  return existing;
}

export function resolveDetectBridgePort(
  managementIp: string | undefined,
  devices: AccessPointDetectDevice[],
): string | undefined {
  const ip = managementIp?.trim();
  if (!ip) return undefined;
  const match = devices.find((device) => device.ip.trim() === ip);
  if (match?.bridgePort?.trim()) return match.bridgePort.trim();
  const persisted = loadPersistedDetectBridgePorts()[ip];
  return persisted?.trim() || undefined;
}

async function pollAccessPointJobOnce(jobId: number): Promise<AccessPointJob> {
  const startedAt = Date.now();
  let networkFailures = 0;
  let last: AccessPointJob | null = null;

  while (Date.now() - startedAt < POLL_DEADLINE_MS) {
    let job: AccessPointJob;
    try {
      job = await api.get<AccessPointJob>(`${embeddedApi.accessPoints}/jobs/${jobId}`, {
        timeoutMs: 15_000,
      });
      networkFailures = 0;
    } catch (err) {
      if (isNetworkError(err) && networkFailures < NETWORK_RETRY_LIMIT) {
        networkFailures += 1;
        await sleep(POLL_MS);
        continue;
      }
      if (isApiError(err) && err.status === 404) {
        throw new Error("Access point job was not found.");
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
      "Access point job timed out waiting for status. The operation was not treated as success.",
  );
}

async function queueAndWait(
  queue: () => Promise<AccessPointCheckQueued>,
  busyMessage: string,
): Promise<AccessPointJob> {
  const queued = await queue();
  const jobId = queued.jobId;
  if (!jobId) {
    throw new ApiError(busyMessage, 500, "JOB_FAILED");
  }
  const existing = activePollers.get(jobId);
  if (existing) return existing;
  const poller = pollAccessPointJobOnce(jobId).finally(() => {
    activePollers.delete(jobId);
  });
  activePollers.set(jobId, poller);
  return poller;
}

export const accessPointsApi = {
  list: () => api.get<AccessPointList>(embeddedApi.accessPoints),
  get: (id: string) =>
    api.get<AccessPointRecord>(`${embeddedApi.accessPoints}/${encodeURIComponent(id)}`),
  create: (payload: AccessPointWritePayload) =>
    api.post<AccessPointRecord>(embeddedApi.accessPoints, payload),
  update: (id: string, payload: AccessPointWritePayload) =>
    api.put<AccessPointRecord>(`${embeddedApi.accessPoints}/${encodeURIComponent(id)}`, payload),
  remove: (id: string) =>
    api.delete<{ ok: boolean; id: string }>(
      `${embeddedApi.accessPoints}/${encodeURIComponent(id)}`,
    ),
  checkAccessPoint: (id: string) =>
    api.post<AccessPointCheckQueued>(
      `${embeddedApi.accessPoints}/${encodeURIComponent(id)}/check`,
      {},
    ),
  syncAccessPoint: (id: string) =>
    api.post<AccessPointCheckQueued>(
      `${embeddedApi.accessPoints}/${encodeURIComponent(id)}/sync`,
      {},
    ),
  getAccessPointJob: (jobId: number) =>
    api.get<AccessPointJob>(`${embeddedApi.accessPoints}/jobs/${jobId}`),
  checkAndWait: (id: string) =>
    queueAndWait(() => accessPointsApi.checkAccessPoint(id), "Check did not return a job id"),
  syncAndWait: (id: string) =>
    queueAndWait(() => accessPointsApi.syncAccessPoint(id), "Sync did not return a job id"),
  queueDetect: () => api.post<AccessPointDetectQueued>(`${embeddedApi.accessPoints}/detect`, {}),
  getDetectJob: (jobId: number) =>
    api.get<AccessPointDetectJob>(`${embeddedApi.accessPoints}/detect/jobs/${jobId}`),
  detectAndWait: async (): Promise<AccessPointDetectJob> => {
    const queued = await accessPointsApi.queueDetect();
    const jobId = queued.jobId;
    if (!jobId) {
      throw new ApiError("Detect did not return a job id", 500, "JOB_FAILED");
    }
    const existing = activeDetectPollers.get(jobId);
    if (existing) return existing;
    const poller = (async () => {
      const startedAt = Date.now();
      let networkFailures = 0;
      let last: AccessPointDetectJob | null = null;
      while (Date.now() - startedAt < POLL_DEADLINE_MS) {
        let job: AccessPointDetectJob;
        try {
          job = await accessPointsApi.getDetectJob(jobId);
          networkFailures = 0;
        } catch (err) {
          if (isNetworkError(err) && networkFailures < NETWORK_RETRY_LIMIT) {
            networkFailures += 1;
            await sleep(POLL_MS);
            continue;
          }
          if (isApiError(err) && err.status === 404) {
            throw new Error("Access point detect job was not found.");
          }
          throw err;
        }
        last = job;
        const state = normalizeJobState(job);
        if (isRunningState(state)) {
          await sleep(POLL_MS);
          continue;
        }
        if (isTerminalState(state)) return job;
        await sleep(POLL_MS);
      }
      throw new Error("Access point detect timed out waiting for MikroTik ARP results.");
    })().finally(() => {
      activeDetectPollers.delete(jobId);
    });
    activeDetectPollers.set(jobId, poller);
    return poller;
  },
};
