import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const settingsApi = {
  admin: () => api.get<{ username: string }>(`${embeddedApi.settings}/admin`),
  updateAdmin: (payload: { username?: string; password?: string }) =>
    api.put<{ ok: boolean }>(`${embeddedApi.settings}/admin`, payload),
  backupUrl: () => `${embeddedApi.settings}/backup`,
  restore: (backup: unknown) =>
    api.post<{ ok: boolean }>(`${embeddedApi.settings}/restore`, backup),
};
