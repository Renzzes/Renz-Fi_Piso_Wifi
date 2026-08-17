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

/** RouterOS connectivity — observational, not “host exists”. */
export function mikrotikApiStatusDisplay(
  mikrotik: SystemStatus["mikrotik"] | undefined,
  loading: boolean,
): ConnectionStatusDisplay {
  if (loading) return { label: "Loading...", tone: "unknown" };
  if (!mikrotik) return { label: "Not Configured", tone: "not_configured" };

  const host = mikrotik.host?.trim() ?? "";
  const configured = mikrotik.configured ?? Boolean(host);
  if (!configured || !host) {
    return { label: "Not Configured", tone: "not_configured" };
  }

  const connectivity = (mikrotik.connectivity ?? "unknown").toLowerCase();
  if (connectivity === "online") {
    return { label: "Online", tone: "connected" };
  }
  if (connectivity === "offline") {
    return { label: "Offline", tone: "disconnected" };
  }
  return { label: "Unknown", tone: "unknown" };
}

export function hotspotServiceStatusDisplay(
  hotspot: SystemStatus["hotspot"] | undefined,
  loading: boolean,
): ConnectionStatusDisplay {
  if (loading) return { label: "Loading...", tone: "unknown" };
  if (!hotspot) return { label: "Unknown", tone: "unknown" };

  const status = (hotspot.status ?? (hotspot.ok ? "available" : "unknown")).toLowerCase();
  if (status === "available") {
    return { label: "Available", tone: "connected" };
  }
  if (status === "unavailable") {
    return { label: "Unavailable", tone: "disconnected" };
  }
  return { label: "Unknown", tone: "unknown" };
}

export function internetReachabilityDisplay(
  internet: SystemStatus["internet"] | undefined,
  loading: boolean,
  wan?: SystemStatus["wan"],
): ConnectionStatusDisplay {
  if (loading) return { label: "Loading...", tone: "unknown" };

  if (wan?.known) {
    if (wan.internet === "online") {
      return { label: "Online", tone: "connected" };
    }
    if (wan.internet === "unknown" && wan.defaultRoute === "available") {
      return { label: "Reachability Unverified", tone: "unknown" };
    }
    if (wan.internet === "unknown" || wan.defaultRoute === "unknown") {
      return { label: "Unable to verify", tone: "unknown" };
    }
    if (wan.dhcp === "searching") {
      return { label: "WAN DHCP searching", tone: "disconnected" };
    }
    if (wan.defaultRoute === "unavailable") {
      return { label: "No upstream route", tone: "disconnected" };
    }
    return { label: "Offline", tone: "disconnected" };
  }

  if (!isInternetStatusKnown(internet)) {
    return { label: "Not Configured", tone: "not_configured" };
  }

  const status = internetStatusDisplay(internet, loading);
  return {
    label: status.label,
    tone: status.variant === "ok" ? "connected" : "disconnected",
  };
}
