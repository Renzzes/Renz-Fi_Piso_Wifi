import type { SystemStatus } from "@/types/api";
import { isInternetStatusKnown, internetStatusDisplay } from "@/lib/adminStatus";

export type ConnectionTone = "connected" | "disconnected" | "not_configured" | "unknown";

export type ConnectionStatusDisplay = {
  label: string;
  tone: ConnectionTone;
};

export function connectionToneToVariant(
  tone: ConnectionTone,
): "ok" | "bad" | "unknown" | "unconfigured" {
  switch (tone) {
    case "connected":
      return "ok";
    case "disconnected":
      return "bad";
    case "not_configured":
      return "unconfigured";
    default:
      return "unknown";
  }
}

export function mikrotikApiStatusDisplay(
  mikrotik: SystemStatus["mikrotik"] | undefined,
  loading: boolean,
): ConnectionStatusDisplay {
  if (loading) return { label: "Loading...", tone: "unknown" };
  if (!mikrotik) return { label: "Not Configured", tone: "not_configured" };

  const host = mikrotik.host?.trim() ?? "";
  if (!host) return { label: "Not Configured", tone: "not_configured" };

  return {
    label: mikrotik.ok ? "Connected" : "Disconnected",
    tone: mikrotik.ok ? "connected" : "disconnected",
  };
}

export function hotspotServiceStatusDisplay(
  hotspot: SystemStatus["hotspot"] | undefined,
  loading: boolean,
): ConnectionStatusDisplay {
  if (loading) return { label: "Loading...", tone: "unknown" };
  if (!hotspot) return { label: "Not Configured", tone: "not_configured" };

  const ssid = hotspot.ssid?.trim() ?? "";
  if (!ssid) return { label: "Not Configured", tone: "not_configured" };

  return {
    label: hotspot.ok ? "Connected" : "Disconnected",
    tone: hotspot.ok ? "connected" : "disconnected",
  };
}

export function internetReachabilityDisplay(
  internet: SystemStatus["internet"] | undefined,
  loading: boolean,
): ConnectionStatusDisplay {
  if (loading) return { label: "Loading...", tone: "unknown" };

  if (!isInternetStatusKnown(internet)) {
    return { label: "Not Configured", tone: "not_configured" };
  }

  const status = internetStatusDisplay(internet, loading);
  return {
    label: status.label,
    tone: status.variant === "ok" ? "connected" : "disconnected",
  };
}
