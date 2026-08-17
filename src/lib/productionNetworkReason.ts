export type ProductionNetworkReason =
  | "ok"
  | "interface-disabled"
  | "interface-not-running"
  | "ssid-mismatch"
  | "ssid-empty"
  | "invalid-mode"
  | "routeros-error"
  | "missing-interface"
  | "api-failure";

export type ProductionNetworkCacheSnapshot = {
  verified?: boolean;
  interface?: string;
  ssid?: string;
  expectedSsid?: string;
  frequency?: number | string;
  channel?: number | string;
  mode?: string;
  verifiedAt?: string;
  reason?: ProductionNetworkReason | string;
};

const REASON_LABELS: Record<string, string> = {
  ok: "Production Wi-Fi is healthy.",
  "interface-disabled": "The production wireless interface is disabled.",
  "interface-not-running": "The production wireless interface is not running.",
  "ssid-mismatch": "The broadcast SSID does not match the configured production network.",
  "ssid-empty": "No production SSID is broadcasting.",
  "invalid-mode": "The wireless interface is not in AP or AP-bridge mode.",
  "routeros-error": "RouterOS reported an error while verifying production Wi-Fi.",
  "missing-interface": "The configured production wireless interface was not found.",
  "api-failure": "Unable to reach the router API for production Wi-Fi verification.",
};

export function productionNetworkReasonLabel(
  reason: string | undefined,
): string {
  if (!reason) return "Production Wi-Fi verification failed.";
  return REASON_LABELS[reason] ?? "Production Wi-Fi verification failed.";
}

export function isProductionNetworkHealthy(
  snapshot: ProductionNetworkCacheSnapshot | null | undefined,
): boolean {
  return snapshot?.verified === true && (snapshot.reason ?? "ok") === "ok";
}
