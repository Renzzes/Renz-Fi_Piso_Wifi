import type { RegisteredDevice } from "@/types/deviceProfile";

const REGISTRY_KEY = "renz_device_registry";
const CURRENT_DEVICE_KEY = "renz_current_device_id";
const FLEET_MODE_KEY = "renz_fleet_mode";
const DISCOVERY_SUBNET_KEY = "renz_discovery_subnet";

export function readDeviceRegistry(): RegisteredDevice[] {
  try {
    const raw = localStorage.getItem(REGISTRY_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as RegisteredDevice[];
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export function writeDeviceRegistry(devices: RegisteredDevice[]): void {
  localStorage.setItem(REGISTRY_KEY, JSON.stringify(devices));
}

export function upsertRegisteredDevice(device: RegisteredDevice): RegisteredDevice[] {
  const list = readDeviceRegistry();
  const index = list.findIndex((entry) => entry.deviceId === device.deviceId);
  if (index >= 0) {
    list[index] = { ...list[index], ...device, name: device.name || list[index].name };
  } else {
    list.push(device);
  }
  writeDeviceRegistry(list);
  return list;
}

export function removeRegisteredDevice(deviceId: string): RegisteredDevice[] {
  const list = readDeviceRegistry().filter((entry) => entry.deviceId !== deviceId);
  writeDeviceRegistry(list);
  if (readCurrentDeviceId() === deviceId) {
    clearCurrentDeviceId();
  }
  return list;
}

export function readCurrentDeviceId(): string | null {
  return localStorage.getItem(CURRENT_DEVICE_KEY);
}

export function writeCurrentDeviceId(deviceId: string): void {
  localStorage.setItem(CURRENT_DEVICE_KEY, deviceId);
}

export function clearCurrentDeviceId(): void {
  localStorage.removeItem(CURRENT_DEVICE_KEY);
}

export function getRegisteredDevice(deviceId: string): RegisteredDevice | undefined {
  return readDeviceRegistry().find((entry) => entry.deviceId === deviceId);
}

export function isFleetModeEnabled(): boolean {
  return localStorage.getItem(FLEET_MODE_KEY) === "true";
}

export function setFleetModeEnabled(enabled: boolean): void {
  localStorage.setItem(FLEET_MODE_KEY, enabled ? "true" : "false");
}

export function readDiscoverySubnet(): string {
  return localStorage.getItem(DISCOVERY_SUBNET_KEY) ?? "192.168.88";
}

export function writeDiscoverySubnet(subnet: string): void {
  localStorage.setItem(DISCOVERY_SUBNET_KEY, subnet.trim());
}
