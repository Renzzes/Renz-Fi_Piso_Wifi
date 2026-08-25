import type { ActiveUser } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const usersApi = {
  active: () => api.get<ActiveUser[]>(embeddedApi.users),
  disconnect: (mac: string) =>
    api.post<{ ok: boolean }>(`${embeddedApi.users}/disconnect`, { mac }),
  reconnect: (mac: string) =>
    api.post<{ ok: boolean }>(`${embeddedApi.users}/reconnect`, { mac }),
  terminate: (mac: string) =>
    api.post<{ ok: boolean }>(`${embeddedApi.users}/terminate`, { mac }),
  pause: (mac: string) => api.post<{ ok: boolean }>(`${embeddedApi.users}/pause`, { mac }),
  resume: (mac: string) => api.post<{ ok: boolean }>(`${embeddedApi.users}/resume`, { mac }),
};
