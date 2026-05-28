import { apiUrl } from "./embeddedApi";

export class ApiError extends Error {
  constructor(
    message: string,
    public status: number,
    public code?: string,
  ) {
    super(message);
  }
}

export async function apiFetch<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(apiUrl(path), {
    credentials: "include",
    ...init,
    headers: {
      "Content-Type": "application/json",
      ...init?.headers,
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
  return (await res.text()) as Promise<T>;
}

export const api = {
  get: <T>(path: string) => apiFetch<T>(path),
  post: <T>(path: string, body?: unknown) =>
    apiFetch<T>(path, {
      method: "POST",
      body: body !== undefined ? JSON.stringify(body) : undefined,
    }),
  put: <T>(path: string, body?: unknown) =>
    apiFetch<T>(path, {
      method: "PUT",
      body: body !== undefined ? JSON.stringify(body) : undefined,
    }),
  delete: <T>(path: string) => apiFetch<T>(path, { method: "DELETE" }),
};
