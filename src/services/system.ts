import type { SystemStatus } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export type SdRetryResult = {
  healthy: boolean;
  fallback: boolean;
};

export type NetworkStatus = {
  mode?: string;
  modeLabel?: string;
  ethernet?: {
    linkUp?: boolean;
    ip?: string;
    gateway?: string;
    subnet?: string;
    mac?: string;
  };
  ip?: string;
  gateway?: string;
  subnet?: string;
};

export const systemApi = {
  status: () => api.get<SystemStatus>(embeddedApi.status),
  network: () => api.get<NetworkStatus>(`${embeddedApi.system}/network`),
  reboot: () => api.post<{ ok: boolean }>(`${embeddedApi.system}/reboot`),
  factoryReset: () => api.post<{ ok: boolean }>(`${embeddedApi.system}/factory-reset`),
  retrySd: () => api.post<SdRetryResult>("/api/storage/retry-sd"),
};
