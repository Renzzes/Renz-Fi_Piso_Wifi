import { DEFAULT_API_TIMEOUT_MS } from "@/services/api";
import { embeddedApi } from "@/services/embeddedApi";
import {
  deviceProfileFromHealth,
  registeredDeviceFromProfile,
  type DeviceProfile,
  type RegisteredDevice,
} from "@/types/deviceProfile";
import { upsertRegisteredDevice } from "@/services/deviceRegistry";

export type DiscoveryProgress = {
  scanned: number;
  total: number;
  found: RegisteredDevice[];
};

export type DiscoveryOptions = {
  subnet?: string;
  start?: number;
  end?: number;
  concurrency?: number;
  onProgress?: (progress: DiscoveryProgress) => void;
  signal?: AbortSignal;
};

const DEFAULT_CONCURRENCY = 16;

function healthUrlForIp(ip: string): string {
  return `http://${ip}${embeddedApi.health}`;
}

export async function probeDeviceAtIp(
  ip: string,
  signal?: AbortSignal,
): Promise<{ profile: DeviceProfile; device: RegisteredDevice } | null> {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), DEFAULT_API_TIMEOUT_MS);
  const onAbort = () => controller.abort();
  signal?.addEventListener("abort", onAbort);

  try {
    const res = await fetch(healthUrlForIp(ip), {
      method: "GET",
      signal: controller.signal,
      credentials: "omit",
    });
    if (!res.ok) return null;

    const json = (await res.json()) as Record<string, unknown>;
    const data = (json.data as Record<string, unknown> | undefined) ?? json;
    const profile = deviceProfileFromHealth(data);
    if (!profile) return null;

    const withIp: DeviceProfile = {
      ...profile,
      ipAddress: profile.ipAddress || ip,
      online: true,
    };

    return {
      profile: withIp,
      device: registeredDeviceFromProfile(withIp, ip),
    };
  } catch {
    return null;
  } finally {
    clearTimeout(timeout);
    signal?.removeEventListener("abort", onAbort);
  }
}

export async function discoverDevicesOnSubnet(
  options: DiscoveryOptions = {},
): Promise<RegisteredDevice[]> {
  const subnet = options.subnet ?? "192.168.88";
  const start = options.start ?? 1;
  const end = options.end ?? 254;
  const concurrency = options.concurrency ?? DEFAULT_CONCURRENCY;
  const ips = Array.from({ length: end - start + 1 }, (_, i) => `${subnet}.${start + i}`);
  const found: RegisteredDevice[] = [];
  let scanned = 0;

  const queue = [...ips];

  async function worker() {
    while (queue.length > 0) {
      if (options.signal?.aborted) return;
      const ip = queue.shift();
      if (!ip) return;

      const result = await probeDeviceAtIp(ip, options.signal);
      scanned += 1;
      if (result) {
        upsertRegisteredDevice(result.device);
        found.push(result.device);
      }

      options.onProgress?.({
        scanned,
        total: ips.length,
        found: [...found],
      });
    }
  }

  await Promise.all(Array.from({ length: concurrency }, () => worker()));
  return found;
}
