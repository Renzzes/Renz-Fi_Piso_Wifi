import type { SystemStatus } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const systemApi = {
  status: () => api.get<SystemStatus>(embeddedApi.status),
  reboot: () => api.post<{ ok: boolean }>(`${embeddedApi.system}/reboot`),
  factoryReset: () => api.post<{ ok: boolean }>(`${embeddedApi.system}/factory-reset`),
};
