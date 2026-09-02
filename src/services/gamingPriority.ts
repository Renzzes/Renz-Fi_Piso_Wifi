import { api, isApiError, isNetworkError } from "./api";
import { embeddedApi } from "./embeddedApi";

export const GAMING_PRIORITY_QUERY_KEY = ["gaming-priority"] as const;

export type GamingPriorityLevel = "highest" | "high" | "normal";
export type GamingApplyStatus =
  | "disabled"
  | "enabled"
  | "pending_changes"
  | "applied"
  | "error"
  | "out_of_sync"
  | "unknown";

export type GameProfile = {
  id: string;
  name: string;
  slug: string;
  enabled: boolean;
  classificationMethod: string;
  classificationData: {
    protocol: string;
    ports: string;
  };
  priority: GamingPriorityLevel;
};

export type GamingPriorityState = {
  schemaVersion?: number;
  enabled: boolean;
  priority: GamingPriorityLevel;
  minimumGamingMbps: number;
  maximumGamingMbps: number;
  perUserGamingMbps: number;
  updatedAt?: number;
  configRevision?: number;
  appliedRevision?: number;
  lastApplyAt?: number;
  lastApplyOk?: boolean;
  lastApplyError?: string;
  applyStatus?: GamingApplyStatus;
  gameProfiles: GameProfile[];
};

export type GamingPriorityJobQueued = {
  jobId: number;
  state?: string;
};

export type GamingPriorityJob = {
  jobId: number;
  state?: string;
  ok?: boolean;
  httpStatus?: number;
  result?: string | Record<string, unknown>;
};

const POLL_MS = 450;
const POLL_DEADLINE_MS = 45_000;
const NETWORK_RETRY_LIMIT = 4;
const activePollers = new Map<number, Promise<GamingPriorityJob>>();

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function normalizeJobState(job: GamingPriorityJob): string {
  return (job.state ?? "").trim().toLowerCase();
}

function isTerminalState(state: string): boolean {
  return state === "completed" || state === "failed";
}

function isRunningState(state: string): boolean {
  return state === "queued" || state === "running";
}

async function pollGamingPriorityJobOnce(jobId: number): Promise<GamingPriorityJob> {
  const startedAt = Date.now();
  let networkFailures = 0;

  while (Date.now() - startedAt < POLL_DEADLINE_MS) {
    let job: GamingPriorityJob;
    try {
      job = await api.get<GamingPriorityJob>(
        `${embeddedApi.gamingPriority}/jobs/${jobId}`,
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
        throw new Error("Gaming priority job was not found.");
      }
      throw err;
    }

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
    "Gaming priority apply timed out waiting for MikroTik. Check router worker status.",
  );
}

async function queueAndWaitApply(): Promise<GamingPriorityJob> {
  const queued = await api.post<GamingPriorityJobQueued>(
    `${embeddedApi.gamingPriority}/apply`,
    {},
    { timeoutMs: 15_000 },
  );
  const jobId = queued.jobId;
  if (!jobId) throw new Error("Gaming priority apply was not queued.");

  const existing = activePollers.get(jobId);
  if (existing) return existing;

  const pollPromise = pollGamingPriorityJobOnce(jobId).finally(() => {
    activePollers.delete(jobId);
  });
  activePollers.set(jobId, pollPromise);
  return pollPromise;
}

export function gamingPriorityJobSucceeded(job: GamingPriorityJob): boolean {
  if (job.state === "failed") return false;
  if (job.ok === false) return false;
  return job.ok !== false;
}

export function gamingPriorityStatusLabel(state: GamingPriorityState | undefined): string {
  if (!state) return "Loading...";
  switch (state.applyStatus) {
    case "pending_changes":
      return "Pending changes";
    case "applied":
      return "Applied";
    case "error":
      return state.lastApplyError ? "Apply error" : "Error";
    case "disabled":
      return "Disabled";
    default:
      return state.enabled ? "Enabled" : "Disabled";
  }
}

export const gamingPriorityApi = {
  get: () => api.get<GamingPriorityState>(embeddedApi.gamingPriority),

  save: (payload: GamingPriorityState) =>
    api.put<GamingPriorityState>(embeddedApi.gamingPriority, payload, {
      timeoutMs: 15_000,
    }),

  apply: () => queueAndWaitApply(),
};
