import { systemApi } from "./system";
import { routerApi } from "./router";
import type { SystemStatus } from "@/types/api";

/**
 * Admin synchronization = sync the Admin UI with authoritative Renz-Fi Core state.
 *
 * Does NOT:
 * - send MikroTik passwords to the browser
 * - open RouterOS on every connect
 * - create a second credential store
 *
 * MAY (only when Core reports router cache stale + credentials present):
 * - enqueue existing POST /api/router/cache/sync (202 → worker → cache)
 * - still load the Dashboard if that worker refresh fails (degraded RouterOS)
 */
export type AdminSyncPhase = "device" | "router" | "dashboard" | "ready";

export type AdminSyncResult = {
  ok: true;
  routerCredentialsPresent: boolean;
  routerConnectivity: string;
  cachePopulated: boolean;
  cacheStale: boolean;
  workerRefreshRequested: boolean;
  workerRefreshOk: boolean | null;
  status: SystemStatus;
};

export const ADMIN_SYNC_PHASES: { id: AdminSyncPhase; label: string }[] = [
  { id: "device", label: "Synchronizing Renz-Fi state…" },
  { id: "router", label: "Checking router state…" },
  { id: "dashboard", label: "Loading dashboard…" },
  { id: "ready", label: "Connected." },
];

export async function synchronizeAdminClient(
  onPhase?: (phase: AdminSyncPhase) => void,
): Promise<AdminSyncResult> {
  onPhase?.("device");
  let status = await systemApi.status();

  const host = status.mikrotik?.host?.trim() ?? "";
  const routerCredentialsPresent = Boolean(status.mikrotik?.configured || host);
  let routerConnectivity = (status.mikrotik?.connectivity ?? "unknown").toLowerCase();
  let cachePopulated = Boolean(status.routerCache?.populated);
  let cacheStale = Boolean(status.routerCache?.stale);

  const storage = status.storageStatus;
  const storageBlockingRouter =
    Boolean(storage?.recoveryInProgress) ||
    Boolean(storage?.recoveryMode) ||
    storage?.mounted === false;

  onPhase?.("router");

  let workerRefreshRequested = false;
  let workerRefreshOk: boolean | null = null;

  // Fresh cache / unconfigured router → never open RouterOS on connect.
  // Stale + credentials present → existing worker job only (never async_tcp RouterOS).
  // Storage remount/degraded → backend already returns 503 without queuing;
  // skip the enqueue so Admin does not hammer deferred jobs.
  if (cacheStale && routerCredentialsPresent && !storageBlockingRouter) {
    workerRefreshRequested = true;
    try {
      await routerApi.syncRouter();
      workerRefreshOk = true;
      status = await systemApi.status();
      routerConnectivity = (status.mikrotik?.connectivity ?? "unknown").toLowerCase();
      cachePopulated = Boolean(status.routerCache?.populated);
      cacheStale = Boolean(status.routerCache?.stale);
    } catch {
      // Core healthy; RouterOS may be offline. Dashboard still loads.
      workerRefreshOk = false;
    }
  }

  onPhase?.("dashboard");
  onPhase?.("ready");

  return {
    ok: true,
    routerCredentialsPresent,
    routerConnectivity,
    cachePopulated,
    cacheStale,
    workerRefreshRequested,
    workerRefreshOk,
    status,
  };
}
