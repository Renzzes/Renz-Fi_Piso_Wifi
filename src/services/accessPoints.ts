import { api } from "./api";
import { embeddedApi } from "./embeddedApi";

export const ACCESS_POINT_VENDORS = [
  "generic",
  "tp-link",
  "ruijie",
  "tenda",
  "other",
] as const;

export type AccessPointVendor = (typeof ACCESS_POINT_VENDORS)[number];

export type AccessPointRecord = {
  id: string;
  name: string;
  enabled: boolean;
  vendor: AccessPointVendor | string;
  model: string;
  managementIp: string;
  hasCredentials: boolean;
  ssid: string;
  location: string;
  notes: string;
};

export type AccessPointList = {
  schemaVersion: number;
  accessPoints: AccessPointRecord[];
  registryError?: string;
};

export type AccessPointWritePayload = {
  name: string;
  managementIp: string;
  enabled?: boolean;
  vendor?: string;
  model?: string;
  username?: string;
  password?: string;
  ssid?: string;
  location?: string;
  notes?: string;
};

export const accessPointsApi = {
  list: () => api.get<AccessPointList>(embeddedApi.accessPoints),
  get: (id: string) =>
    api.get<AccessPointRecord>(`${embeddedApi.accessPoints}/${encodeURIComponent(id)}`),
  create: (payload: AccessPointWritePayload) =>
    api.post<AccessPointRecord>(embeddedApi.accessPoints, payload),
  update: (id: string, payload: AccessPointWritePayload) =>
    api.put<AccessPointRecord>(
      `${embeddedApi.accessPoints}/${encodeURIComponent(id)}`,
      payload,
    ),
  remove: (id: string) =>
    api.delete<{ ok: boolean; id: string }>(
      `${embeddedApi.accessPoints}/${encodeURIComponent(id)}`,
    ),
};
