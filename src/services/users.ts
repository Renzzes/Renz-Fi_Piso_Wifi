import type { ActiveUser } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const usersApi = {
  active: () => api.get<ActiveUser[]>(embeddedApi.users),
  disconnect: (mac: string) =>
    api.post<{ ok: boolean }>(`${embeddedApi.users}/disconnect`, { mac }),
};
