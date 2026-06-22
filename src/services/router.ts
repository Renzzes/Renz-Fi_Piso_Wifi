import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export type RouterConfig = {
  host: string;
  username: string;
  password: string;
  profile: string;
  ssid?: string;
  wifiPassword?: string;
  passwordConfigured?: boolean;
};

export type RouterTestStep = {
  id: "api_reachable" | "login" | "profile";
  label: string;
  ok: boolean;
  message: string;
};

export type RouterOsTestResult = {
  ok: boolean;
  connected?: boolean;
  authenticated?: boolean;
  profileFound?: boolean;
  identity?: string;
  error?: string;
};

export type RouterTestResult = RouterOsTestResult & {
  steps?: RouterTestStep[];
  summary?: string;
};

export type RouterProfilesResult = {
  profiles: string[];
  error?: string;
};

function normalizeProfilesResult(raw: unknown): RouterProfilesResult {
  if (!raw || typeof raw !== "object") {
    return { profiles: [], error: "Invalid profiles response" };
  }
  const record = raw as Record<string, unknown>;
  if (Array.isArray(record.profiles)) {
    return {
      profiles: record.profiles.filter((p): p is string => typeof p === "string"),
      error: typeof record.error === "string" ? record.error : undefined,
    };
  }
  // Defensive: handle accidental double-wrapped payloads.
  if (record.data && typeof record.data === "object") {
    return normalizeProfilesResult(record.data);
  }
  return { profiles: [], error: "Profiles array missing from response" };
}

export const routerApi = {
  settings: () => api.get<RouterConfig>(`${embeddedApi.router}/settings`),
  profiles: async () => {
    const response = await api.get<RouterProfilesResult>(`${embeddedApi.router}/profiles`);
    const normalized = normalizeProfilesResult(response);
    console.log("Router profiles response:", response);
    return normalized;
  },
  save: (config: Partial<RouterConfig>) =>
    api.put<{ ok: boolean }>(`${embeddedApi.router}/settings`, config),
  test: (config?: Partial<RouterConfig>) =>
    api.post<RouterTestResult>(`${embeddedApi.router}/test`, config),
};
