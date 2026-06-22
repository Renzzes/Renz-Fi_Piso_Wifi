import type { SystemStatus } from "@/types/api";

/** True when the API exposes a real WAN/internet signal (not the ESP32 stub). */
export function isInternetStatusKnown(internet: SystemStatus["internet"] | undefined): boolean {
  if (!internet) return false;
  if (internet.known === false) return false;
  if (internet.known === true) return true;
  // Firmware stub today: ok=true, latencyMs=0 — not a reliable WAN probe.
  return !(internet.ok === true && internet.latencyMs === 0);
}

export function internetStatusDisplay(
  internet: SystemStatus["internet"] | undefined,
  loading: boolean,
): { label: string; ok: boolean; variant: "ok" | "bad" | "unknown"; latency: string } {
  if (loading) {
    return { label: "Loading...", ok: false, variant: "unknown", latency: "Loading..." };
  }

  if (!isInternetStatusKnown(internet)) {
    return { label: "Unknown", ok: false, variant: "unknown", latency: "—" };
  }

  const ok = Boolean(internet?.ok);
  return {
    label: ok ? "Connected" : "Disconnected",
    ok,
    variant: ok ? "ok" : "bad",
    latency: internet?.latencyMs === undefined ? "—" : `${internet.latencyMs} ms`,
  };
}

export function adminConnectionStatusDisplay(
  adminApiReachable: boolean,
  loading: boolean,
): { label: string; ok: boolean; variant: "ok" | "bad" } {
  if (loading) return { label: "Loading...", ok: false, variant: "bad" };
  return {
    label: adminApiReachable ? "Connected" : "Disconnected",
    ok: adminApiReachable,
    variant: adminApiReachable ? "ok" : "bad",
  };
}

/** @deprecated Use adminConnectionStatusDisplay */
export const adminApiStatusDisplay = adminConnectionStatusDisplay;

export type WanStatusDisplay = {
  show: boolean;
  label: string;
  ok: boolean;
  variant: "ok" | "bad" | "unknown";
  latency: string;
};

/** WAN card/row — only when firmware reports a real probe (internet.known === true). */
export function wanStatusDisplay(
  internet: SystemStatus["internet"] | undefined,
  loading: boolean,
): WanStatusDisplay {
  if (!isInternetStatusKnown(internet)) {
    return { show: false, label: "—", ok: false, variant: "unknown", latency: "—" };
  }
  const status = internetStatusDisplay(internet, loading);
  return { show: true, ...status };
}
