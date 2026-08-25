import { apiUrl, embeddedApi } from "@/services/embeddedApi";
import {
  parseApplianceBuildMetadata,
  type ApplianceBuildMetadata,
} from "@/types/buildMetadata";

export type ApplianceBuildSnapshot = {
  runningFirmwareVersion: string | null;
  staged: ApplianceBuildMetadata | null;
};

export async function fetchApplianceBuildSnapshot(): Promise<ApplianceBuildSnapshot> {
  const res = await fetch(apiUrl(embeddedApi.health), { credentials: "include" });
  if (!res.ok) {
    throw new Error(`Health check failed: ${res.status}`);
  }

  const json = (await res.json()) as {
    data?: Record<string, unknown>;
    version?: string;
  };
  const data: Record<string, unknown> = json.data ?? (json as Record<string, unknown>);

  const device =
    data.device && typeof data.device === "object"
      ? (data.device as Record<string, unknown>)
      : undefined;

  const runningFirmwareVersion =
    (typeof data.version === "string" && data.version.trim()) ||
    (typeof device?.firmwareVersion === "string" &&
      device.firmwareVersion.trim()) ||
    (typeof data.firmwareVersion === "string" && data.firmwareVersion.trim()) ||
    null;

  return {
    runningFirmwareVersion,
    staged: parseApplianceBuildMetadata(data.build),
  };
}
