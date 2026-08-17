import { apiUrl, embeddedApi } from "@/services/embeddedApi";
import {
  fetchApplianceHealthSnapshot,
  parseApplianceHealthSnapshot,
} from "@/services/fleetHealthService";
import {
  deviceProfileFromHealth,
  registeredDeviceFromProfile,
  type RegisteredDevice,
} from "@/types/deviceProfile";
import type { ApplianceHealthSnapshot } from "@/types/fleetHealth";
import { upsertRegisteredDevice } from "@/services/deviceRegistry";

export type DeviceHealthProbeResult = {
  device: RegisteredDevice;
  snapshot: ApplianceHealthSnapshot;
};

export async function fetchDeviceProfileFromCurrentTarget(): Promise<RegisteredDevice | null> {
  const res = await fetch(apiUrl(embeddedApi.health), { credentials: "include" });
  if (!res.ok) return null;

  const json = (await res.json()) as Record<string, unknown>;
  const data = (json.data as Record<string, unknown> | undefined) ?? json;
  const profile = deviceProfileFromHealth(data);
  if (!profile) return null;

  const ip =
    profile.ipAddress ||
    (typeof window !== "undefined" ? window.location.hostname : "");

  const device = registeredDeviceFromProfile(profile, ip);
  upsertRegisteredDevice(device);
  return device;
}

export async function probeDeviceHealth(
  device: RegisteredDevice,
): Promise<DeviceHealthProbeResult> {
  const snapshot = await fetchApplianceHealthSnapshot(device.ip);
  if (!snapshot.probeOk || !snapshot.profile) {
    const offline = { ...device, isOnline: false, lastSeen: device.lastSeen };
    upsertRegisteredDevice(offline);
    return { device: offline, snapshot };
  }
  const updated = registeredDeviceFromProfile(snapshot.profile, device.ip, device);
  upsertRegisteredDevice(updated);
  return { device: updated, snapshot };
}

export async function refreshDeviceOnlineStatus(
  device: RegisteredDevice,
): Promise<RegisteredDevice> {
  const result = await probeDeviceHealth(device);
  return result.device;
}

export async function refreshAllDeviceStatuses(
  devices: RegisteredDevice[],
): Promise<RegisteredDevice[]> {
  const results = await Promise.all(devices.map((device) => probeDeviceHealth(device)));
  return results.map((r) => r.device);
}

export async function refreshAllDeviceHealth(
  devices: RegisteredDevice[],
): Promise<DeviceHealthProbeResult[]> {
  return Promise.all(devices.map((device) => probeDeviceHealth(device)));
}

/** @deprecated Use probeDeviceHealth — kept for callers parsing inline. */
export { parseApplianceHealthSnapshot };
