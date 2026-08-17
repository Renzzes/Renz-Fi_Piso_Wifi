import type { StorageStatus, SystemStatus } from "@/types/api";
import { api } from "./api";
import { embeddedApi } from "./embeddedApi";
import { logDashboardApi } from "@/lib/dashboardApiLog";

export type SdRetryResult = {
  healthy: boolean;
  fallback: boolean;
};

export type NetworkStatus = {
  mode?: string;
  modeLabel?: string;
  interfaces?: {
    managementAp?: ManagementApStatus;
    ethernet?: Record<string, unknown>;
  };
  managementAp?: ManagementApStatus;
  ethernet?: {
    link?: boolean;
    linkUp?: boolean;
    ip?: string;
    gateway?: string;
    subnet?: string;
    dns?: string;
    mac?: string;
  };
  ip?: string;
  gateway?: string;
  subnet?: string;
};

export type ManagementApStatus = {
  mode?: "factory" | "maintenance" | "disabled";
  running?: boolean;
  enabled?: boolean;
  ssid?: string;
  ip?: string;
  clients?: number;
  uptimeSeconds?: number | null;
  timeoutSeconds?: number | null;
};

/** ESP32's own Ethernet configuration (NOT RouterOS/wireless). */
export type EthernetConfig = {
  addressMode: "dhcp" | "static";
  provisioned: boolean;
  staticIp?: string;
  staticGateway?: string;
  staticSubnetMask?: string;
  staticDnsPrimary?: string;
  staticDnsSecondary?: string;
  current?: {
    mode: string;
    ip: string;
    gateway: string;
    netmask: string;
    dns: string;
    mac: string;
  };
};

export type EthernetConfigSave = {
  addressMode: "dhcp" | "static";
  staticIp?: string;
  staticGateway?: string;
  staticSubnetMask?: string;
  staticDnsPrimary?: string;
  staticDnsSecondary?: string;
};

export type FactoryResetQueued = {
  ok?: boolean;
  jobId: number;
  status?: string;
  state?: string;
};

export type FactoryResetStatus = {
  jobId?: number;
  status?: string;
  state?: string;
  phase?: string;
  progress?: number;
  rebooting?: boolean;
  error?: string;
};

export const systemApi = {
  status: async () => {
    try {
      return await api.get<SystemStatus>(embeddedApi.status);
    } catch (err) {
      logDashboardApi("GET /api/status", err);
      throw err;
    }
  },
  network: () => api.get<NetworkStatus>(`${embeddedApi.system}/network`),
  wifiConfig: () => api.get<EthernetConfig>(`${embeddedApi.system}/wifi/config`),
  saveWifiConfig: (config: EthernetConfigSave) =>
    api.put<EthernetConfig & { rebootRequired?: boolean }>(
      `${embeddedApi.system}/wifi/config`,
      config,
    ),
  managementApPostSetup: (keepEnabled: boolean) =>
    api.post<{ keepEnabledAfterSetup?: boolean }>(
      `${embeddedApi.system}/management-ap/post-setup`,
      { keepEnabled },
    ),
  managementApStart: () =>
    api.post<ManagementApStatus>(`${embeddedApi.system}/management-ap/start`, {}),
  managementApTemporary: (durationSeconds = 600) =>
    api.post<ManagementApStatus & { durationSeconds?: number }>(
      `${embeddedApi.system}/management-ap/temporary`,
      { durationSeconds },
    ),
  managementApStop: () =>
    api.post<ManagementApStatus>(`${embeddedApi.system}/management-ap/stop`, {}),
  reboot: () => api.post<{ ok: boolean }>(`${embeddedApi.system}/reboot`),
  factoryReset: () =>
    api.post<FactoryResetQueued>(`${embeddedApi.system}/factory-reset`),
  factoryResetStatus: (jobId?: number) => {
    const query = jobId != null ? `?jobId=${jobId}` : "";
    return api.get<FactoryResetStatus>(
      `${embeddedApi.system}/factory-reset/status${query}`,
    );
  },
  retrySd: () => api.post<SdRetryResult>("/api/storage/retry-sd"),
  storageStatus: () => api.get<StorageStatus>("/api/storage/status"),
};
