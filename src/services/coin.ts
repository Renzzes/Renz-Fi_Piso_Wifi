import type { LogEntry } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const coinApi = {
  settings: () => api.get<Record<string, string>>(`${embeddedApi.coin}/settings`),
  save: (settings: Record<string, string>) =>
    api.put<{ ok: boolean }>(`${embeddedApi.coin}/settings`, settings),
  diagnostics: () =>
    api.get<{ stats: Record<string, string>; logs: LogEntry[] }>(`${embeddedApi.coin}/diagnostics`),
  test: () => api.post<{ ok: boolean }>(`${embeddedApi.coin}/test`),
};
