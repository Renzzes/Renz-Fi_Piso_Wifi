import { systemApi } from "./system";
import type { SystemStatus } from "@/types/api";

/**
 * Admin synchronization = thin Core state for Connect (no RouterOS, no sales chart).
 *
 * Does NOT:
 * - send MikroTik passwords to the browser
 * - open RouterOS on every connect
 * - enqueue POST /api/router/cache/sync (owner uses Synchronize Router)
 * - create a second credential store
 *
 * Connect loads GET /api/status only so the Dashboard can paint from Core RAM/SPIFFS
 * without a DMA-heavy fan-out. Live SSE / secondary panels are opt-in on the Dashboard.
 */
export type AdminSyncPhase = "device" | "dashboard" | "ready";

export type AdminSyncResult = {
  ok: true;
  routerCredentialsPresent: boolean;
  routerConnectivity: string;
  cachePopulated: boolean;
  cacheStale: boolean;
  workerRefreshRequested: false;
  workerRefreshOk: null;
  status: SystemStatus;
};

export const ADMIN_SYNC_PHASES: { id: AdminSyncPhase; label: string }[] = [
  { id: "device", label: "Synchronizing Renz-Fi state…" },
  { id: "dashboard", label: "Loading dashboard…" },
  { id: "ready", label: "Connected." },
];

export async function synchronizeAdminClient(
  onPhase?: (phase: AdminSyncPhase) => void,
): Promise<AdminSyncResult> {
  onPhase?.("device");
  const status = await systemApi.status();

  const host = status.mikrotik?.host?.trim() ?? "";
  const routerCredentialsPresent = Boolean(status.mikrotik?.configured || host);
  const routerConnectivity = (status.mikrotik?.connectivity ?? "unknown").toLowerCase();
  const cachePopulated = Boolean(status.routerCache?.populated);
  const cacheStale = Boolean(status.routerCache?.stale);

  onPhase?.("dashboard");
  onPhase?.("ready");

  return {
    ok: true,
    routerCredentialsPresent,
    routerConnectivity,
    cachePopulated,
    cacheStale,
    workerRefreshRequested: false,
    workerRefreshOk: null,
    status,
  };
}
