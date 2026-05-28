import type { LogEntry } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const logsApi = {
  list: (q?: string) =>
    api.get<LogEntry[]>(`${embeddedApi.logs}${q ? `?q=${encodeURIComponent(q)}` : ""}`),
  clear: () => api.delete<{ ok: boolean }>(embeddedApi.logs),
  exportUrl: () => `${embeddedApi.logs}/export`,
};
