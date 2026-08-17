import {
  Activity,
  AlertTriangle,
  Coins,
  Globe,
  HardDrive,
  Monitor,
  Router,
  Users,
  Wifi,
} from "lucide-react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { StatCard, StatusRow } from "@/components/StatCard";
import { PageHeader } from "@/components/PageHeader";
import { RouterCacheStaleBanner } from "@/components/RouterCacheStaleBanner";
import { StorageHealthCard } from "@/components/StorageHealthCard";
import { CollapsiblePanel } from "@/components/CollapsiblePanel";
import { Progress } from "@/components/ui/progress";
import { useSystemStatus } from "@/hooks/api/useSystemStatus";
import { useRealtime } from "@/contexts/RealtimeContext";
import { coinApi } from "@/services/coin";
import { routerApi } from "@/services/router";
import { formatPeso } from "@/lib/currency";
import { routerCacheLastSyncLabel } from "@/lib/routerCacheStatus";
import {
  isProductionNetworkHealthy,
  productionNetworkReasonLabel,
} from "@/lib/productionNetworkReason";
import { refreshProductionRouterViews } from "@/lib/refreshProductionRouterViews";
import { adminConnectionStatusDisplay, wanStatusDisplay } from "@/lib/adminStatus";
import { formatRgbColorLabel } from "@/lib/rgbDisplay";
import type { CoinState, SystemStatus } from "@/types/api";
import { healthApi, rgbApi } from "@/services/rgb";

function usagePct(used: number, total: number) {
  if (total <= 0) return 0;
  return Math.min(100, Math.round((used / total) * 100));
}

function formatMb(value: number | undefined) {
  if (value === undefined) return "—";
  return value.toFixed(1);
}

function formatSessions(sessions: number | undefined, loading: boolean) {
  if (loading) return "Loading...";
  if (sessions === undefined) return "—";
  return `${sessions} sessions`;
}

function formatRouterMemory(freeBytes?: string, totalBytes?: string): string {
  const free = Number(freeBytes);
  const total = Number(totalBytes);
  if (!Number.isFinite(free) || !Number.isFinite(total) || total <= 0) return "—";
  const usedMb = (total - free) / 1024 / 1024;
  const totalMb = total / 1024 / 1024;
  return `${usedMb.toFixed(0)} / ${totalMb.toFixed(0)} MB`;
}

function mikrotikDisplay(
  mikrotik: SystemStatus["mikrotik"] | undefined,
  loading: boolean,
): { label: string; ok: boolean; host: string; connectivityLabel: string; connectivityOk: boolean } {
  if (loading) {
    return {
      label: "Loading...",
      ok: false,
      host: "Loading...",
      connectivityLabel: "Loading...",
      connectivityOk: false,
    };
  }
  if (!mikrotik) {
    return {
      label: "—",
      ok: false,
      host: "—",
      connectivityLabel: "—",
      connectivityOk: false,
    };
  }

  const host = mikrotik.host?.trim() ?? "";
  const configured = mikrotik.configured ?? Boolean(host);
  if (!host || !configured) {
    return {
      label: "Not Configured",
      ok: false,
      host: "Not Configured",
      connectivityLabel: "—",
      connectivityOk: false,
    };
  }

  const connectivity = (mikrotik.connectivity ?? "unknown").toLowerCase();
  let connectivityLabel = "Unknown";
  let connectivityOk = false;
  if (connectivity === "online") {
    connectivityLabel = "Online";
    connectivityOk = true;
  } else if (connectivity === "offline") {
    connectivityLabel = "Offline";
  }

  return {
    label: "Configured",
    ok: true,
    host,
    connectivityLabel,
    connectivityOk,
  };
}

function hotspotDisplay(
  hotspot: SystemStatus["hotspot"] | undefined,
  loading: boolean,
): { label: string; ok: boolean } {
  if (loading) return { label: "Loading...", ok: false };
  if (!hotspot) return { label: "Unknown", ok: false };

  const status = (hotspot.status ?? (hotspot.ok ? "available" : "unknown")).toLowerCase();
  if (status === "available") return { label: "Available", ok: true };
  if (status === "unavailable") return { label: "Unavailable", ok: false };
  return { label: "Unknown", ok: false };
}

function coinStateLabel(state: CoinState | string | undefined) {
  switch (state) {
    case "WAITING_FOR_ACTIVITY":
      return "Waiting For Activity";
    case "RESPONDING":
      return "Responding";
    case "NO_RECENT_ACTIVITY":
      return "No Recent Activity";
    case "FAULT":
      return "Fault";
    case "DISABLED":
    default:
      return "Disabled";
  }
}

function coinStateOk(state: CoinState | string | undefined) {
  return state === "RESPONDING" || state === "WAITING_FOR_ACTIVITY" || state === "NO_RECENT_ACTIVITY";
}

function coinDisplay(
  status: SystemStatus | undefined,
  diagState: string | undefined,
  diagPulses: string | undefined,
  loading: boolean,
): {
  featureLabel: string;
  hardwareLabel: string;
  ok: boolean;
  pulses: string;
  lastPulse: string;
  lastCoin: string;
  totalCoins: string;
  totalPulses: string;
} {
  if (loading) {
    return {
      featureLabel: "Loading...",
      hardwareLabel: "Loading...",
      ok: false,
      pulses: "Loading...",
      lastPulse: "Loading...",
      lastCoin: "Loading...",
      totalCoins: "Loading...",
      totalPulses: "Loading...",
    };
  }

  const coin = status?.coinSlot;
  const enabled = coin?.enabled ?? coin?.ok ?? false;
  const pulses =
    diagPulses ??
    (coin?.pulsesToday !== undefined ? String(coin.pulsesToday) : undefined);

  return {
    featureLabel: enabled ? "Enabled" : "Disabled",
    hardwareLabel: coinStateLabel(coin?.hardwareState ?? coin?.state) || diagState || coin?.stateLabel || "—",
    ok: enabled && coinStateOk(coin?.hardwareState ?? coin?.state),
    pulses: pulses !== undefined ? `${pulses} pulses today` : "—",
    lastPulse: coin?.lastPulseTimestamp ?? "—",
    lastCoin: coin?.lastCoinTimestamp ?? "—",
    totalCoins:
      coin?.totalCoinCount !== undefined ? String(coin.totalCoinCount) : "—",
    totalPulses:
      coin?.totalPulseCount !== undefined ? String(coin.totalPulseCount) : "—",
  };
}

function boolStatus(
  ok: boolean | undefined,
  loading: boolean,
  okLabel: string,
  failLabel: string,
): { label: string; ok: boolean } {
  if (loading) return { label: "Loading...", ok: false };
  if (ok === undefined) return { label: "—", ok: false };
  return { label: ok ? okLabel : failLabel, ok };
}

function DashboardLoadError({ message }: { message: string }) {
  return (
    <div className="rounded-md border border-amber-300 bg-amber-50 text-amber-900 px-3 py-2 text-sm mb-3">
      {message}
    </div>
  );
}

export default function DashboardPage() {
  const queryClient = useQueryClient();
  const { fallbackPollMs, adminApiReachable } = useRealtime();
  const {
    data: status,
    isLoading: statusLoading,
    isError: statusError,
    error: statusErr,
  } = useSystemStatus();
  const {
    data: coinDiagnostics,
    isLoading: coinDiagLoading,
    isError: coinDiagError,
  } = useQuery({
    queryKey: ["coin", "diagnostics"],
    queryFn: () => coinApi.diagnostics(),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });
  const {
    data: systemHealth,
    isLoading: healthLoading,
    isError: healthError,
  } = useQuery({
    queryKey: ["system", "health"],
    queryFn: () => healthApi.get(),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });
  const {
    data: coinSystem,
    isLoading: coinSystemLoading,
    isError: coinSystemError,
  } = useQuery({
    queryKey: ["system", "coin"],
    queryFn: () => coinApi.system(),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });
  const {
    data: rgbSystem,
    isLoading: rgbSystemLoading,
    isError: rgbSystemError,
  } = useQuery({
    queryKey: ["system", "rgb"],
    queryFn: () => rgbApi.system(),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });

  const coinLoading = statusLoading || coinDiagLoading || coinSystemLoading;
  const today = status?.sales?.today;
  const weekly = status?.sales?.weekly;
  const monthly = status?.sales?.monthly;
  const storage = status?.storage;
  const sd = storage?.sd;

  const mikrotik = mikrotikDisplay(status?.mikrotik, statusLoading);
  const routerCache = status?.routerCache;
  const storageRecovering =
    Boolean(status?.storageStatus?.recoveryInProgress) ||
    Boolean(status?.storageStatus?.recoveryMode) ||
    status?.storageStatus?.mounted === false;
  const syncRouterMutation = useMutation({
    mutationFn: () => routerApi.syncRouter(),
    onSuccess: async () => {
      await refreshProductionRouterViews(queryClient);
    },
  });
  const routerOs = routerCache?.routerOs;
  const mikrotikHint = [
    mikrotik.ok ? `Connectivity: ${mikrotik.connectivityLabel}` : null,
    routerCache?.identity ? `Identity: ${routerCache.identity}` : null,
    routerCache?.populated
      ? routerCache.productionNetwork
        ? isProductionNetworkHealthy(routerCache.productionNetwork)
          ? `Production Wi-Fi healthy · Last sync ${routerCacheLastSyncLabel(routerCache)}`
          : `${productionNetworkReasonLabel(routerCache.productionNetwork.reason)} · Last sync ${routerCacheLastSyncLabel(routerCache)}`
        : `Last sync ${routerCacheLastSyncLabel(routerCache)}`
      : mikrotik.host !== "Not Configured"
        ? mikrotik.host
        : null,
  ]
    .filter(Boolean)
    .join(" · ");
  const hotspot = hotspotDisplay(status?.hotspot, statusLoading);
  const adminConnection = adminConnectionStatusDisplay(adminApiReachable, statusLoading);
  const wan = wanStatusDisplay(status?.internet, statusLoading, status?.wan);
  const coin = coinDisplay(
    status,
    coinDiagnostics?.stats?.state,
    coinDiagnostics?.stats?.total_today,
    coinLoading,
  );

  const flashPct = usagePct(storage?.flashUsedMb ?? 0, storage?.flashTotalMb ?? 0);
  const ramPct = usagePct(storage?.ramUsedKb ?? 0, storage?.ramTotalKb ?? 0);
  const logsPct = usagePct(storage?.logsUsedKb ?? 0, storage?.logsTotalKb ?? 0);
  const sdPct = usagePct(sd?.usedMb ?? 0, sd?.totalMb ?? 0);
  const sdReady = sd?.mounted === true && sd?.status === "Ready";

  const database = boolStatus(status?.database?.ok, statusLoading, "Healthy", "Error");
  const server = boolStatus(status?.server?.ok, statusLoading, "Running", "Down");

  const activeCount = statusLoading
    ? "Loading..."
    : status?.activeUsers?.count !== undefined
      ? status.activeUsers.count
      : "—";

  const pausedUsers = status?.activeUsers?.paused;
  const pausedHint = statusLoading
    ? "Loading..."
    : pausedUsers !== undefined
      ? `${pausedUsers} paused`
      : "—";

  const pausedUsersStatus = statusLoading
    ? "Loading..."
    : pausedUsers !== undefined
      ? String(pausedUsers)
      : "—";

  const uptimeStatus = statusLoading ? "Loading..." : (status?.esp32?.uptime ?? "—");

  const syncStatus = statusLoading
    ? "Loading..."
    : status?.sync?.pending !== undefined
      ? `${status.sync.pending} items`
      : "—";

  const wanTone =
    wan.variant === "unknown" ? "default" : wan.ok ? "success" : "danger";

  const statusCardCount = wan.show ? 5 : 4;
  const rgbColor = formatRgbColorLabel(rgbSystem, rgbSystemLoading);

  if (rgbSystem) {
    console.log("[RGB]", rgbSystem);
  }

  const loadErrors: string[] = [];
  if (statusError) loadErrors.push("Unable to load data from /api/status");
  if (healthError) loadErrors.push("Unable to load data from /api/system/health");
  if (coinSystemError) loadErrors.push("Unable to load data from /api/system/coin");
  if (rgbSystemError) loadErrors.push("Unable to load data from /api/system/rgb");
  if (coinDiagError) loadErrors.push("Unable to load coin diagnostics");

  if (statusErr) {
    console.warn("[dashboard] status query error detail", statusErr);
  }

  return (
    <div>
      <PageHeader title="Dashboard" description="Overview of your Renz-Fi system" />

      {loadErrors.length > 0 ? (
        <DashboardLoadError
          message={`${loadErrors.join(". ")}. Showing the last known values where available.`}
        />
      ) : null}

      <RouterCacheStaleBanner
        cache={routerCache}
        pending={syncRouterMutation.isPending || storageRecovering}
        onRefresh={() => syncRouterMutation.mutate()}
      />

      <div className="grid grid-cols-2 md:grid-cols-4 gap-2 sm:gap-3">
        <StatCard
          label="Today's Sales"
          value={formatPeso(today?.amount, statusLoading)}
          icon={Coins}
          tone="success"
          hint={formatSessions(today?.sessions, statusLoading)}
        />
        <StatCard
          label="Weekly Sales"
          value={formatPeso(weekly?.amount, statusLoading)}
          icon={Coins}
          hint={formatSessions(weekly?.sessions, statusLoading)}
        />
        <StatCard
          label="Monthly Sales"
          value={formatPeso(monthly?.amount, statusLoading)}
          icon={Coins}
          hint={formatSessions(monthly?.sessions, statusLoading)}
        />
        <StatCard
          label="Active Users"
          value={activeCount}
          icon={Users}
          tone={
            !statusLoading && pausedUsers !== undefined && pausedUsers > 0 ? "warning" : "success"
          }
          hint={pausedHint}
        />
      </div>

      <div className="grid md:grid-cols-2 xl:grid-cols-3 gap-3 mt-3">
        <CollapsiblePanel
          id="dash-system-status"
          title="System Status"
          summary={`MikroTik: ${mikrotik.connectivityLabel} · Hotspot: ${hotspot.label}`}
        >
          <StatusRow label="MikroTik Router" status={mikrotik.label} ok={mikrotik.ok} />
          <StatusRow
            label="Connectivity"
            status={mikrotik.connectivityLabel}
            ok={mikrotik.connectivityOk}
            variant={
              mikrotik.connectivityOk
                ? "ok"
                : mikrotik.connectivityLabel === "Offline"
                  ? "bad"
                  : "unknown"
            }
          />
          <StatusRow
            label="Router Identity"
            status={statusLoading ? "Loading..." : routerCache?.identity || "—"}
            ok={Boolean(routerCache?.identity)}
          />
          <StatusRow
            label="RouterOS Version"
            status={
              statusLoading
                ? "Loading..."
                : routerOs?.version || routerCache?.routerOsVersion || "—"
            }
            ok={Boolean(routerOs?.version || routerCache?.routerOsVersion)}
          />
          <StatusRow
            label="CPU"
            status={
              statusLoading
                ? "Loading..."
                : routerOs?.cpuLoad
                  ? `${routerOs.cpuLoad}%`
                  : "—"
            }
            ok={Boolean(routerOs?.cpuLoad)}
          />
          <StatusRow
            label="Memory"
            status={
              statusLoading
                ? "Loading..."
                : formatRouterMemory(routerOs?.freeMemory, routerOs?.totalMemory)
            }
            ok={formatRouterMemory(routerOs?.freeMemory, routerOs?.totalMemory) !== "—"}
          />
          <StatusRow
            label="Uptime"
            status={statusLoading ? "Loading..." : routerOs?.uptime || "—"}
            ok={Boolean(routerOs?.uptime)}
          />
          <StatusRow
            label="Production SSID"
            status={
              statusLoading
                ? "Loading..."
                : routerCache?.productionNetwork?.ssid || routerCache?.ssid || "—"
            }
            ok={Boolean(routerCache?.productionNetwork?.ssid || routerCache?.ssid)}
          />
          <StatusRow label="Hotspot" status={hotspot.label} ok={hotspot.ok} variant={hotspot.ok ? "ok" : "unknown"} />
          <StatusRow
            label="Admin Connection"
            status={adminConnection.label}
            ok={adminConnection.ok}
            variant={adminConnection.variant}
          />
          {wan.show ? (
            <StatusRow
              label="WAN Internet"
              status={wan.label}
              ok={wan.ok}
              variant={wan.variant}
            />
          ) : null}
          <StatusRow label="Coin Slot Feature" status={coin.featureLabel} ok={coin.ok} />
          <StatusRow
            label="Coin Hardware"
            status={coin.hardwareLabel}
            ok={coin.hardwareLabel === "Responding"}
          />
          <StatusRow label="Paused Users" status={pausedUsersStatus} ok={pausedUsers === 0} />
          <StatusRow
            label="ESP32 Uptime"
            status={uptimeStatus}
            ok={Boolean(status?.esp32?.lastSeen)}
          />
          <StatusRow
            label="Pending Sync"
            status={syncStatus}
            ok={status?.sync?.pending !== undefined && status.sync.pending === 0}
          />
          <StatusRow label="Database" status={database.label} ok={database.ok} />
          <StatusRow label="API Server" status={server.label} ok={server.ok} />
        </CollapsiblePanel>

        <CollapsiblePanel
          id="dash-coin-slot"
          title="Coin Slot"
          summary={`${coin.featureLabel} · ${coin.hardwareLabel}`}
        >
          <StatusRow label="Feature Status" status={coin.featureLabel} ok={coin.ok} />
          <StatusRow
            label="Hardware Status"
            status={coin.hardwareLabel}
            ok={coin.hardwareLabel === "Responding"}
          />
          <StatusRow label="Last Pulse" status={coin.lastPulse} ok={coin.lastPulse !== "—"} />
          <StatusRow label="Last Coin" status={coin.lastCoin} ok={coin.lastCoin !== "—"} />
          <StatusRow label="Total Coins" status={coin.totalCoins} ok={coin.totalCoins !== "0"} />
          <StatusRow label="Total Pulses" status={coin.totalPulses} ok={coin.totalPulses !== "0"} />
        </CollapsiblePanel>

        <CollapsiblePanel
          id="dash-storage-health"
          title="SD Card Health"
          summary={statusLoading ? "Loading…" : (sd?.status ?? "Storage")}
        >
          <StorageHealthCard className="border-0 p-0" />
        </CollapsiblePanel>

        <CollapsiblePanel
          id="dash-system-health"
          title="System Health"
          summary={healthLoading ? "Loading…" : (systemHealth?.level ?? "—")}
        >
          <StatusRow
            label="Overall"
            status={healthLoading ? "Loading..." : (systemHealth?.level ?? "—")}
            ok={
              systemHealth?.level === "HEALTHY" ||
              systemHealth?.level === "ACTIVE_SESSION"
            }
          />
          <StatusRow
            label="Ethernet Driver"
            status={healthLoading ? "Loading..." : (systemHealth?.ethernet?.driver ?? "—")}
            ok={systemHealth?.ethernet?.driver === "UP"}
          />
          <StatusRow
            label="Ethernet Link"
            status={healthLoading ? "Loading..." : (systemHealth?.ethernet?.link ?? "—")}
            ok={systemHealth?.ethernet?.link === "UP"}
          />
          <StatusRow
            label="Heap"
            status={
              healthLoading || systemHealth?.memory?.heap === undefined
                ? "Loading..."
                : `${systemHealth.memory.heap} B`
            }
            ok={Boolean(systemHealth && (systemHealth.memory?.heap ?? 0) > 50000)}
          />
        </CollapsiblePanel>

        <CollapsiblePanel
          id="dash-coin-state"
          title="Coin State"
          summary={coinSystemLoading ? "Loading…" : coinStateLabel(coinSystem?.state)}
        >
          <StatusRow
            label="Coin State"
            status={
              coinSystemLoading
                ? "Loading..."
                : coinStateLabel(coinSystem?.state)
            }
            ok={coinStateOk(coinSystem?.state)}
          />
          <StatusRow
            label="Enabled"
            status={coinSystemLoading ? "Loading..." : coinSystem?.enabled ? "Yes" : "No"}
            ok={Boolean(coinSystem?.enabled)}
          />
          <StatusRow
            label="Total Coins"
            status={
              coinSystemLoading
                ? "Loading..."
                : coinSystem?.totalCoinCount !== undefined
                  ? String(coinSystem.totalCoinCount)
                  : coin.totalCoins
            }
            ok={Boolean(coinSystem?.totalCoinCount)}
          />
          <StatusRow
            label="Total Pulses"
            status={
              coinSystemLoading
                ? "Loading..."
                : coinSystem?.totalPulseCount !== undefined
                  ? String(coinSystem.totalPulseCount)
                  : coin.totalPulses
            }
            ok={Boolean(coinSystem?.totalPulseCount)}
          />
          <StatusRow
            label="Last Activity"
            status={
              coinSystemLoading
                ? "Loading..."
                : coinSystem?.lastPulseTimestamp || coinSystem?.lastCoinTimestamp || "—"
            }
            ok={Boolean(coinSystem?.lastPulseTimestamp || coinSystem?.lastCoinTimestamp)}
          />
        </CollapsiblePanel>

        <CollapsiblePanel
          id="dash-rgb"
          title="RGB Status"
          summary={rgbSystemLoading ? "Loading…" : (rgbSystem?.state ?? "—")}
        >
          <StatusRow
            label="Current State"
            status={rgbSystemLoading ? "Loading..." : (rgbSystem?.state ?? "—")}
            ok={rgbSystem?.state === "IDLE" || rgbSystem?.state === "COIN_ACCEPTED"}
          />
          <StatusRow
            label="Current Color"
            status={rgbColor}
            ok={rgbColor !== "—"}
          />
          <StatusRow
            label="Brightness"
            status={
              rgbSystemLoading
                ? "Loading..."
                : rgbSystem?.brightness !== undefined
                  ? `${rgbSystem.brightness}%`
                  : "—"
            }
            ok={Boolean(rgbSystem?.brightness)}
          />
          <StatusRow
            label="Enabled"
            status={rgbSystemLoading ? "Loading..." : rgbSystem?.enabled ? "Yes" : "No"}
            ok={Boolean(rgbSystem?.enabled)}
          />
        </CollapsiblePanel>

        <CollapsiblePanel
          id="dash-storage-usage"
          title="Storage Usage"
          summary={
            statusLoading
              ? "Loading…"
              : sdReady
                ? "SD Ready"
                : (sd?.status ?? "Storage")
          }
        >
          <div className="space-y-3 text-xs">
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground">Flash (SPIFFS)</span>
                <span className="tabular-nums">
                  {statusLoading
                    ? "Loading..."
                    : storage
                      ? `${formatMb(storage.flashUsedMb)} / ${formatMb(storage.flashTotalMb)} MB`
                      : "—"}
                </span>
              </div>
              <Progress value={flashPct} className="h-1.5" />
            </div>
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground">Internal Heap</span>
                <span className="tabular-nums">
                  {statusLoading
                    ? "Loading..."
                    : storage
                      ? `${storage.internalHeap?.usedKb ?? storage.ramUsedKb} / ${storage.internalHeap?.totalKb ?? storage.ramTotalKb} KB`
                      : "—"}
                </span>
              </div>
              <Progress value={ramPct} className="h-1.5" />
              {!statusLoading && storage?.psram?.present ? (
                <div className="flex justify-between mt-2 text-[10px] text-muted-foreground">
                  <span>PSRAM</span>
                  <span className="tabular-nums">
                    {storage.psram.usedKb} / {storage.psram.totalKb} KB
                  </span>
                </div>
              ) : null}
              {!statusLoading && storage?.dma ? (
                <div className="flex justify-between mt-1 text-[10px] text-muted-foreground">
                  <span>DMA free / largest</span>
                  <span className="tabular-nums">
                    {storage.dma.freeKb} / {storage.dma.largestKb} KB
                  </span>
                </div>
              ) : null}
            </div>
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground">Logs</span>
                <span className="tabular-nums">
                  {statusLoading
                    ? "Loading..."
                    : storage
                      ? `${storage.logsUsedKb} / ${storage.logsTotalKb} KB`
                      : "—"}
                </span>
              </div>
              <Progress value={logsPct} className="h-1.5" />
            </div>
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground flex items-center gap-1">
                  {!sdReady && sd ? <AlertTriangle className="h-3 w-3 text-amber-500" /> : null}
                  SD Card
                </span>
                <span className="tabular-nums">
                  {statusLoading
                    ? "Loading..."
                    : sdReady
                      ? `${formatMb(sd.usedMb)} / ${formatMb(sd.totalMb)} MB`
                      : sd
                        ? sd.status === "Mount Failed"
                          ? "Mount Failed"
                          : "Not Available"
                        : "—"}
                </span>
              </div>
              {sdReady ? (
                <Progress value={sdPct} className="h-1.5" />
              ) : (
                <div className="text-[10px] text-muted-foreground mt-1">
                  Status: {statusLoading ? "Loading..." : (sd?.status ?? "—")}
                </div>
              )}
              {sdReady ? (
                <div className="text-[10px] text-muted-foreground mt-1">
                  Status: {sd.status} · Free {formatMb(sd.freeMb)} MB
                </div>
              ) : null}
            </div>
          </div>
        </CollapsiblePanel>
      </div>

      <div
        className={`grid grid-cols-2 md:grid-cols-3 gap-2 sm:gap-3 mt-3 ${
          statusCardCount >= 5 ? "xl:grid-cols-5" : "xl:grid-cols-4"
        }`}
      >
        <StatCard
          label="MikroTik"
          value={mikrotik.label}
          icon={Router}
          tone={mikrotik.ok ? "success" : "danger"}
          hint={mikrotikHint}
        />
        <StatCard
          label="Admin Connection"
          value={adminConnection.label}
          icon={Monitor}
          tone={adminConnection.ok ? "success" : "danger"}
          hint="Browser ↔ ESP32 API"
        />
        {wan.show ? (
          <StatCard
            label="WAN Internet"
            value={wan.label}
            icon={Globe}
            tone={wanTone}
            hint={wan.latency}
          />
        ) : null}
        <StatCard
          label="Coin Slot"
          value={coin.hardwareLabel}
          icon={Coins}
          tone={coin.ok ? "success" : "default"}
          hint={coin.pulses}
        />
        <StatCard
          label="Hotspot"
          value={hotspot.label}
          icon={Wifi}
          tone={hotspot.ok ? "success" : "danger"}
          hint="MikroTik hotspot"
        />
      </div>
    </div>
  );
}
