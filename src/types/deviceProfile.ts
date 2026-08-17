/**
 * Frozen canonical appliance identity — see ESP32_S3_Firmware/docs/DEVICE_PROFILE_CONTRACT.md
 * Do not rename or remove fields. Extend via `capabilities` and optional new keys only.
 */

/** Feature flags — clients ignore unknown keys. */
export interface DeviceCapabilities {
  coin: boolean;
  voucher: boolean;
  assetUpload: boolean;
  router: string | null;
  fleet: boolean;
  /** Future SKUs may add keys (lte, wireguard, …) — treat as optional. */
  [key: string]: boolean | string | null | undefined;
}

/** Permanent appliance identity — exposed by GET /api/health `device` field. */
export interface DeviceProfile {
  deviceId: string;
  serialNumber: string;
  friendlyName: string;
  firmwareVersion: string;
  hardwareRevision: string;
  macAddress: string;
  ipAddress: string;
  routerDriver: string | null;
  online: boolean;
  capabilities: DeviceCapabilities;
  /** Legacy alias some clients expect */
  deviceName?: string;
  version?: string;
}

/** Client-side registry entry — keyed by deviceId, not IP. */
export interface RegisteredDevice {
  deviceId: string;
  name: string;
  ip: string;
  serialNumber: string;
  firmwareVersion: string;
  hardwareRevision: string;
  macAddress: string;
  routerDriver: string | null;
  capabilities: DeviceCapabilities | null;
  lastSeen: string | null;
  favorite: boolean;
  isOnline: boolean;
}

const DEFAULT_CAPABILITIES: DeviceCapabilities = {
  coin: false,
  voucher: true,
  assetUpload: true,
  router: null,
  fleet: true,
};

export function deviceCapabilitiesFromHealth(
  raw: Record<string, unknown> | undefined,
  routerDriver: string | null,
): DeviceCapabilities {
  if (!raw || typeof raw !== "object") {
    return { ...DEFAULT_CAPABILITIES, router: routerDriver };
  }
  return {
    coin: raw.coin === true,
    voucher: raw.voucher !== false,
    assetUpload: raw.assetUpload !== false,
    router:
      raw.router != null && String(raw.router).length > 0
        ? String(raw.router)
        : routerDriver,
    fleet: raw.fleet !== false,
  };
}

export function deviceProfileFromHealth(data: Record<string, unknown>): DeviceProfile | null {
  const device = (data.device as Record<string, unknown> | undefined) ?? data;
  const deviceId = String(device.deviceId ?? "").trim();
  if (!deviceId) return null;

  const friendlyName = String(
    device.friendlyName ?? device.deviceName ?? device.name ?? "Renz-Fi Appliance",
  );
  const routerDriver = device.routerDriver != null ? String(device.routerDriver) : null;

  return {
    deviceId,
    serialNumber: String(device.serialNumber ?? device.macAddress ?? deviceId),
    friendlyName,
    firmwareVersion: String(
      device.firmwareVersion ?? device.version ?? data.firmwareVersion ?? "",
    ),
    hardwareRevision: String(device.hardwareRevision ?? ""),
    macAddress: String(device.macAddress ?? ""),
    ipAddress: String(device.ipAddress ?? ""),
    routerDriver,
    online: device.online !== false,
    capabilities: deviceCapabilitiesFromHealth(
      device.capabilities as Record<string, unknown> | undefined,
      routerDriver,
    ),
    deviceName: friendlyName,
    version: String(device.firmwareVersion ?? device.version ?? ""),
  };
}

export function registeredDeviceFromProfile(
  profile: DeviceProfile,
  ip: string,
  existing?: RegisteredDevice,
): RegisteredDevice {
  return {
    deviceId: profile.deviceId,
    name: existing?.name ?? profile.friendlyName,
    ip: profile.ipAddress || ip,
    serialNumber: profile.serialNumber,
    firmwareVersion: profile.firmwareVersion,
    hardwareRevision: profile.hardwareRevision,
    macAddress: profile.macAddress,
    routerDriver: profile.routerDriver,
    capabilities: profile.capabilities,
    lastSeen: new Date().toISOString(),
    favorite: existing?.favorite ?? false,
    isOnline: profile.online,
  };
}
