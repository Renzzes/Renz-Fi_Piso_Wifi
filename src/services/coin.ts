import type { CoinSystemStatus, LogEntry } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";
import { logDashboardApi } from "@/lib/dashboardApiLog";

export const coinApi = {
  system: async () => {
    try {
      return await api.get<CoinSystemStatus>(`${embeddedApi.system}/coin`);
    } catch (err) {
      logDashboardApi("GET /api/system/coin", err);
      throw err;
    }
  },
  settings: () => api.get<Record<string, string>>(`${embeddedApi.coin}/settings`),
  save: (settings: Record<string, string>) =>
    api.put<{ ok: boolean }>(`${embeddedApi.coin}/settings`, settings),
  diagnostics: async () => {
    try {
      return await api.get<{ stats: Record<string, string>; logs: LogEntry[] }>(
        `${embeddedApi.coin}/diagnostics`,
      );
    } catch (err) {
      logDashboardApi("GET /api/coin/diagnostics", err);
      throw err;
    }
  },
  test: () => api.post<{ ok: boolean }>(`${embeddedApi.coin}/test`),
  reset: () => api.post<{ ok: boolean }>(`${embeddedApi.coin}/reset`),
};
