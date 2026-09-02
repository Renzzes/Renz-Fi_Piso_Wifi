import { api, isApiError, isNetworkError } from "./api";
import { embeddedApi } from "./embeddedApi";

export const CONTENT_FILTER_QUERY_KEY = ["content-filter"] as const;

export type ContentFilterDomainStatus = "pending" | "active" | "failed" | "disabled";

export type ContentFilterDomain = {
  domain: string;
  status: ContentFilterDomainStatus;
  addedAt?: number;
  lastError?: string;
};

export type ContentFilterState = {
  schemaVersion?: number;
  enabled: boolean;
  lastSyncAt?: number;
  lastSyncError?: string;
  domains: ContentFilterDomain[];
};

export type ContentFilterJobQueued = {
  jobId: number;
  state?: string;
  domain?: string;
};

export type ContentFilterJob = {
  jobId: number;
  state?: string;
  ok?: boolean;
  httpStatus?: number;
  result?: string | Record<string, unknown>;
};

const POLL_MS = 450;
const POLL_DEADLINE_MS = 45_000;
const NETWORK_RETRY_LIMIT = 4;

const activePollers = new Map<number, Promise<ContentFilterJob>>();

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function normalizeJobState(job: ContentFilterJob): string {
  return (job.state ?? "").trim().toLowerCase();
}

function isTerminalState(state: string): boolean {
  return state === "completed" || state === "failed";
}

function isRunningState(state: string): boolean {
  return state === "queued" || state === "running";
}

async function pollContentFilterJobOnce(jobId: number): Promise<ContentFilterJob> {
  const startedAt = Date.now();
  let networkFailures = 0;
  let last: ContentFilterJob | null = null;

  while (Date.now() - startedAt < POLL_DEADLINE_MS) {
    let job: ContentFilterJob;
    try {
      job = await api.get<ContentFilterJob>(`${embeddedApi.contentFilter}/jobs/${jobId}`, {
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
        throw new Error("Content filter job was not found.");
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
    "Content filter sync timed out waiting for MikroTik apply. Check router worker status.",
  );
}

async function queueAndWait(
  queue: () => Promise<ContentFilterJobQueued>,
): Promise<ContentFilterJob> {
  const queued = await queue();
  const jobId = queued.jobId;
  if (!jobId) throw new Error("Content filter job was not queued.");

  const existing = activePollers.get(jobId);
  if (existing) return existing;

  const pollPromise = pollContentFilterJobOnce(jobId).finally(() => {
    activePollers.delete(jobId);
  });
  activePollers.set(jobId, pollPromise);
  return pollPromise;
}

/** Normalize user input to bare domain (client-side hint; server is authoritative). */
export function normalizeDomainInput(raw: string): string {
  let value = raw.trim().toLowerCase();
  value = value.replace(/^https?:\/\//, "");
  value = value.replace(/\/.*$/, "");
  value = value.replace(/^www\./, "");
  value = value.replace(/:\d+$/, "");
  return value;
}

export function isPlausibleDomain(value: string): boolean {
  if (!value || value.length > 253) return false;
  if (value.includes("/") || value.includes(" ")) return false;
  return /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)+$/.test(
    value,
  );
}

export function contentFilterJobSucceeded(job: ContentFilterJob): boolean {
  if (job.state === "failed") return false;
  if (job.ok === false) return false;
  return job.ok !== false;
}

export function contentFilterStatusLabel(state: ContentFilterState | undefined): string {
  if (!state) return "Loading...";
  if (state.lastSyncError) return "Unable to verify";
  return state.enabled ? "Enabled" : "Disabled";
}

export function recentBlockedDomains(domains: ContentFilterDomain[], limit = 3): string[] {
  return [...domains]
    .sort((a, b) => (b.addedAt ?? 0) - (a.addedAt ?? 0))
    .slice(0, limit)
    .map((row) => row.domain);
}

export const contentFilterApi = {
  get: () => api.get<ContentFilterState>(embeddedApi.contentFilter),

  setEnabled: (enabled: boolean) =>
    queueAndWait(() =>
      api.put<ContentFilterJobQueued>(
        embeddedApi.contentFilter,
        { enabled },
        { timeoutMs: 15_000 },
      ),
    ),

  addDomain: (domain: string) =>
    queueAndWait(() =>
      api.post<ContentFilterJobQueued>(
        `${embeddedApi.contentFilter}/domains`,
        { domain },
        { timeoutMs: 15_000 },
      ),
    ),

  removeDomain: (domain: string) =>
    queueAndWait(() =>
      api.delete<ContentFilterJobQueued>(
        `${embeddedApi.contentFilter}/domains/${encodeURIComponent(domain)}`,
        { timeoutMs: 15_000 },
      ),
    ),

  sync: () =>
    queueAndWait(() =>
      api.post<ContentFilterJobQueued>(
        `${embeddedApi.contentFilter}/sync`,
        {},
        { timeoutMs: 15_000 },
      ),
    ),
};
