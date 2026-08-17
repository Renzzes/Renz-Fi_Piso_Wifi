import type { Voucher } from "@/types/api";
import { api, apiFetch, ApiError, isApiError, isNetworkError } from "./api";
import { embeddedApi } from "./embeddedApi";

export type VoucherGeneratePayload = {
  count: number;
  amount: number;
  minutes: number;
  expires?: string;
  code?: string;
  profileName?: string;
  /** Presentation-only label; does not change RouterOS rate-limit. */
  speed?: string;
  /** When set, firmware/UI maps via ensure-managed profile. */
  downloadMbps?: number;
  uploadMbps?: number;
};

export type VoucherJobResult = {
  created?: string[];
  deleted?: string[];
  skipped?: unknown[];
  count?: number;
};

type VoucherJobQueued = {
  jobId: number;
  state?: string;
  status?: string;
  duplicate?: boolean;
  type?: string;
};

type VoucherJobPoll = {
  jobId: number;
  state?: string;
  status?: string;
  ok?: boolean;
  type?: string;
  error?: string;
  count?: number;
  result?: { created?: string[]; deleted?: string[]; skipped?: unknown[] };
};

const POLL_MS = 400;
/** Wall-clock ceiling while job is explicitly running/queued (not a success signal). */
const POLL_DEADLINE_MS = 120_000;
const NETWORK_RETRY_LIMIT = 4;

/** Exactly one in-flight poller per jobId (StrictMode / double-submit safe). */
const activePollers = new Map<number, Promise<VoucherJobResult>>();

function normalizeJobState(poll: VoucherJobPoll): string {
  const raw = (poll.state ?? poll.status ?? "").trim().toLowerCase();
  return raw;
}

function isTerminalState(state: string): boolean {
  return (
    state === "completed" ||
    state === "failed" ||
    state === "cancelled" ||
    state === "canceled" ||
    state === "success" ||
    state === "error"
  );
}

function isRunningState(state: string): boolean {
  return state === "queued" || state === "running" || state === "pending";
}

function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}

async function pollVoucherJobOnce(jobId: number): Promise<VoucherJobResult> {
  const startedAt = Date.now();
  let lastState = "";
  let networkFailures = 0;

  if (import.meta.env.DEV) {
    console.debug(`[voucher-ui] job=${jobId} polling started`);
  }

  try {
    while (Date.now() - startedAt < POLL_DEADLINE_MS) {
      let poll: VoucherJobPoll;
      try {
        poll = await api.get<VoucherJobPoll>(
          `${embeddedApi.vouchers}/jobs/${jobId}`,
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
          throw new Error(
            "Voucher job status was not found. Refresh the voucher list to confirm the result.",
          );
        }
        throw err;
      }

      const state = normalizeJobState(poll);
      if (state && state !== lastState) {
        if (import.meta.env.DEV) {
          console.debug(
            `[voucher-ui] job=${jobId} status=${state}` +
              (poll.ok === true ? " ok=true" : poll.ok === false ? " ok=false" : ""),
          );
        }
        lastState = state;
      }

      if (isRunningState(state)) {
        await sleep(POLL_MS);
        continue;
      }

      if (state === "failed" || state === "error") {
        throw new Error(poll.error || "Unable to complete voucher job");
      }

      if (state === "cancelled" || state === "canceled") {
        throw new Error(poll.error || "Voucher job was cancelled");
      }

      if (state === "completed" || state === "success" || poll.ok === true) {
        if (poll.ok === false) {
          throw new Error(poll.error || "Unable to complete voucher job");
        }
        const created = poll.result?.created ?? [];
        const deleted = poll.result?.deleted ?? [];
        const count =
          poll.count ??
          (created.length > 0
            ? created.length
            : deleted.length > 0
              ? deleted.length
              : undefined);
        if (import.meta.env.DEV) {
          console.debug(
            `[voucher-ui] job=${jobId} status=completed ok=true` +
              (count != null ? ` count=${count}` : ""),
          );
        }
        return {
          created,
          deleted,
          skipped: poll.result?.skipped ?? [],
          count,
        };
      }

      // Unknown non-terminal state: brief backoff, do not claim "still running".
      await sleep(POLL_MS);
    }

    // Deadline expired while server still reported running/queued — not success.
    throw new Error(
      "Timed out waiting for voucher job status. Refresh the list to confirm whether the job finished.",
    );
  } finally {
    if (import.meta.env.DEV) {
      console.debug(`[voucher-ui] job=${jobId} polling stopped`);
    }
  }
}

function pollVoucherJob(jobId: number): Promise<VoucherJobResult> {
  const existing = activePollers.get(jobId);
  if (existing) return existing;
  const promise = pollVoucherJobOnce(jobId).finally(() => {
    if (activePollers.get(jobId) === promise) activePollers.delete(jobId);
  });
  activePollers.set(jobId, promise);
  return promise;
}

export function formatGeneratedToast(count: number): string {
  const n = Math.max(0, count);
  return n === 1
    ? "Successfully Generated 1 Voucher"
    : `Successfully Generated ${n} Vouchers`;
}

export function formatDeletedToast(count: number): string {
  const n = Math.max(0, count);
  return n === 1
    ? "Successfully Deleted 1 Voucher"
    : `Successfully Deleted ${n} Vouchers`;
}

export function assertGeneratePayload(
  payload: VoucherGeneratePayload,
): asserts payload is VoucherGeneratePayload {
  const { count, amount, minutes } = payload;
  if (
    !Number.isInteger(count) ||
    count < 1 ||
    count > 20 ||
    !Number.isFinite(amount) ||
    amount < 0 ||
    !Number.isInteger(minutes) ||
    minutes < 1 ||
    minutes > 525600
  ) {
    throw new Error(
      "Invalid generate values: count must be 1–20, amount ≥ 0, and validity minutes ≥ 1",
    );
  }
}

export const vouchersApi = {
  list: () => api.get<Voucher[]>(embeddedApi.vouchers),
  generate: async (payload: VoucherGeneratePayload) => {
    assertGeneratePayload(payload);
    if (import.meta.env.DEV) {
      console.debug("[voucher] generate payload", payload);
    }
    const queued = await apiFetch<VoucherJobQueued>(embeddedApi.vouchers, {
      method: "POST",
      body: JSON.stringify(payload),
      timeoutMs: 20_000,
    });
    if (!queued?.jobId) {
      throw new Error("Voucher generation did not return a job id");
    }
    return pollVoucherJob(queued.jobId);
  },
  /** Hard-delete via the same single-flight worker as bulk-delete (202 + jobId). */
  delete: (code: string) => vouchersApi.bulkDelete([code]),
  bulkDelete: async (codes: string[]) => {
    if (!Array.isArray(codes) || codes.length === 0) {
      throw new Error("Select at least 1 voucher to delete.");
    }
    if (codes.length > 20) {
      throw new Error("You can delete a maximum of 20 vouchers at a time.");
    }
    const queued = await apiFetch<VoucherJobQueued>(
      `${embeddedApi.vouchers}/bulk-delete`,
      {
        method: "POST",
        body: JSON.stringify({ codes }),
        timeoutMs: 20_000,
      },
    );
    if (!queued?.jobId) {
      throw new Error("Voucher delete did not return a job id");
    }
    return pollVoucherJob(queued.jobId);
  },
  print: (code: string) => api.get<Voucher>(`${embeddedApi.vouchers}/${code}`),
  terminate: (code: string) =>
    api.post<Voucher>(`${embeddedApi.vouchers}/${encodeURIComponent(code)}/terminate`, {}),
  expire: (code: string) =>
    api.post<Voucher>(`${embeddedApi.vouchers}/${encodeURIComponent(code)}/expire`, {}),
  disable: (code: string) =>
    api.post<Voucher>(`${embeddedApi.vouchers}/${encodeURIComponent(code)}/disable`, {}),
  archive: (code: string) =>
    api.post<Voucher>(`${embeddedApi.vouchers}/${encodeURIComponent(code)}/archive`, {}),
};

// Re-export for callers that type-narrow errors from this module.
export { ApiError };
