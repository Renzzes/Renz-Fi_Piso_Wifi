import { apiUrl } from "./embeddedApi";
import { handleUnauthorizedResponse } from "./authSession";

/** Default request timeout for admin API calls (ms). */
export const DEFAULT_API_TIMEOUT_MS = 60_000;

export class ApiError extends Error {
  constructor(
    message: string,
    public status: number,
    public code?: string,
  ) {
    super(message);
    this.name = "ApiError";
  }
}

/** Offline, timeout, or transport failure — distinct from HTTP 4xx/5xx. */
export class NetworkError extends Error {
  constructor(
    message: string,
    public cause?: unknown,
  ) {
    super(message);
    this.name = "NetworkError";
  }
}

export type ApiFetchOptions = RequestInit & {
  /** Abort request after this many milliseconds (default 60s). */
  timeoutMs?: number;
};

export function isNetworkError(error: unknown): error is NetworkError {
  return error instanceof NetworkError;
}

export function isApiError(error: unknown): error is ApiError {
  return error instanceof ApiError;
}

function offlineMessage(): string {
  return "You appear to be offline. Reconnect to the appliance network and retry.";
}

function timeoutMessage(): string {
  return "Request timed out. Check the appliance connection and retry.";
}

function transportMessage(): string {
  return "Network error. Check the appliance connection and retry.";
}

function isEthDmaLowResponse(status: number, code?: string): boolean {
  return status === 503 && code === "ETH_DMA_LOW";
}

function retryAfterMs(res: Response): number {
  const header = res.headers.get("Retry-After");
  const seconds = header ? Number.parseInt(header, 10) : Number.NaN;
  if (Number.isFinite(seconds) && seconds > 0) return seconds * 1000;
  return 2000;
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

export async function apiFetch<T>(path: string, init?: ApiFetchOptions): Promise<T> {
  const { timeoutMs = DEFAULT_API_TIMEOUT_MS, signal: userSignal, ...restInit } = init ?? {};

  const controller = new AbortController();
  const timeoutId =
    timeoutMs > 0 ? setTimeout(() => controller.abort(), timeoutMs) : undefined;

  const abortFromUser = () => controller.abort();
  userSignal?.addEventListener("abort", abortFromUser);

  const isFormData = typeof FormData !== "undefined" && restInit.body instanceof FormData;
  const isBinaryBody =
    restInit.body instanceof Blob ||
    restInit.body instanceof ArrayBuffer ||
    (typeof ArrayBuffer !== "undefined" &&
      restInit.body != null &&
      ArrayBuffer.isView(restInit.body as ArrayBufferView));

  let ethDmaRetried = false;

  try {
    if (typeof navigator !== "undefined" && navigator.onLine === false) {
      throw new NetworkError(offlineMessage());
    }

    for (;;) {
      const res = await fetch(apiUrl(path), {
        credentials: "include",
        ...restInit,
        signal: controller.signal,
        headers: {
          ...(isFormData || isBinaryBody ? {} : { "Content-Type": "application/json" }),
          ...restInit.headers,
        },
      });

      if (!res.ok) {
        let message = res.statusText;
        let code: string | undefined;
        try {
          const json = (await res.json()) as { error?: string; code?: string };
          message = String(json.error ?? message);
          code = json.code;
        } catch {
          const text = await res.text().catch(() => "");
          if (text) message = text;
        }
        if (!ethDmaRetried && isEthDmaLowResponse(res.status, code)) {
          ethDmaRetried = true;
          await sleep(retryAfterMs(res));
          continue;
        }
        if (res.status === 401) {
          handleUnauthorizedResponse(path);
        }
        throw new ApiError(message || `Request failed (${res.status})`, res.status, code);
      }

      if (res.status === 204) return undefined as T;
      const contentType = res.headers.get("content-type") ?? "";
      if (contentType.includes("application/json")) {
        const json = (await res.json()) as unknown;
        if (
          json &&
          typeof json === "object" &&
          "success" in json &&
          typeof (json as { success?: unknown }).success === "boolean"
        ) {
          const envelope = json as { success: boolean; data?: T; error?: unknown; code?: string };
          if (envelope.success) return envelope.data ?? (undefined as T);
          throw new ApiError(String(envelope.error ?? "Request failed"), res.status, envelope.code);
        }
        return json as T;
      }
      return (await res.text()) as unknown as T;
    }
  } catch (error) {
    if (error instanceof ApiError || error instanceof NetworkError) {
      throw error;
    }
    if (error instanceof DOMException && error.name === "AbortError") {
      if (userSignal?.aborted) {
        throw new NetworkError("Request cancelled.", error);
      }
      throw new NetworkError(timeoutMessage(), error);
    }
    if (typeof navigator !== "undefined" && navigator.onLine === false) {
      throw new NetworkError(offlineMessage(), error);
    }
    throw new NetworkError(transportMessage(), error);
  } finally {
    if (timeoutId) clearTimeout(timeoutId);
    userSignal?.removeEventListener("abort", abortFromUser);
  }
}

export const api = {
  get: <T>(path: string, init?: ApiFetchOptions) => apiFetch<T>(path, init),
  post: <T>(path: string, body?: unknown, init?: ApiFetchOptions) =>
    apiFetch<T>(path, {
      method: "POST",
      body: body !== undefined ? JSON.stringify(body) : undefined,
      ...init,
    }),
  put: <T>(path: string, body?: unknown, init?: ApiFetchOptions) =>
    apiFetch<T>(path, {
      method: "PUT",
      body: body !== undefined ? JSON.stringify(body) : undefined,
      ...init,
    }),
  delete: <T>(path: string, init?: ApiFetchOptions) =>
    apiFetch<T>(path, { method: "DELETE", ...init }),
  upload: <T>(path: string, form: FormData, init?: ApiFetchOptions) =>
    apiFetch<T>(path, { method: "POST", body: form, ...init }),
};
