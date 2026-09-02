import { useOutletContext } from "react-router-dom";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { useState } from "react";
import { RouterCacheStaleBanner } from "@/components/RouterCacheStaleBanner";
import type { AdminOutletContext } from "@/components/AdminLayout";
import {
  ActiveUsersCard,
  CoinSummaryCard,
  ContentFilteringCard,
  MikrotikRouterCard,
  MonthlySalesCard,
  NetworkStatusCard,
  QuickActionsBar,
  SdCardHealthCard,
  SystemHealthCard,
  TotalCoinsCard,
  type SalesPeriod,
} from "@/components/dashboard";
import type { NetworkStatusRow } from "@/components/dashboard/NetworkStatusCard";
import type { MikrotikRouterSnapshot } from "@/components/dashboard/MikrotikRouterCard";
import { useSystemStatus } from "@/hooks/api/useSystemStatus";
import { useRollingMetricHistory } from "@/hooks/useRollingMetricHistory";
import { useRealtime } from "@/contexts/RealtimeContext";
import { ACCESS_POINTS_QUERY_KEY, accessPointsApi } from "@/services/accessPoints";
import { CONTENT_FILTER_QUERY_KEY, contentFilterApi } from "@/services/contentFilter";
import { coinApi } from "@/services/coin";
import { routerApi } from "@/services/router";
import { salesApi } from "@/services/sales";
import { healthApi } from "@/services/rgb";
import { systemApi } from "@/services/system";
import { Button } from "@/components/ui/button";
import { toast } from "sonner";
import { routerCacheLastSyncLabel, isRouterCacheStale } from "@/lib/routerCacheStatus";
import { refreshProductionRouterViews } from "@/lib/refreshProductionRouterViews";
import { wanStatusDisplay } from "@/lib/adminStatus";
import {
  accessPointRegistryDisplay,
  coinDisplay,
  connectivityTone,
  mikrotikDisplay,
  type StatusTone,
} from "@/lib/dashboardDisplay";
import {
  clampPct,
  formatKb,
  formatRouterHddOverview,
  formatRouterMemory,
  formatRouterTemperature,
  formatStorageFromMb,
  usagePct,
} from "@/lib/dashboardFormat";
import { DEFAULT_OPERATOR_PERMISSIONS } from "@/lib/operatorPermissions";
import type { StorageHealth } from "@/types/api";

function DashboardLoadError({ message }: { message: string }) {
  return (
    <div className="mb-3 rounded-md border border-amber-500/40 bg-amber-500/10 px-3 py-2 text-sm text-amber-800 dark:text-amber-200">
      {message}
    </div>
  );
}

function sdBadge(
  health: StorageHealth | undefined,
  sdStatus: string | undefined,
  present: boolean | undefined,
) {
  if (!present) return { label: "Not detected", tone: "unknown" as const };
  if (health === "HEALTHY") return { label: "Healthy", tone: "ok" as const };
  if (health === "WARNING" || health === "DEGRADED" || health === "READ_ONLY") {
    return { label: health === "READ_ONLY" ? "Read only" : "Warning", tone: "warn" as const };
  }
  if (health === "CRITICAL") return { label: "Critical", tone: "bad" as const };
  if (sdStatus === "Ready") return { label: "Healthy", tone: "ok" as const };
  if (sdStatus === "Missing") return { label: "Not detected", tone: "unknown" as const };
  if (sdStatus === "Mount Failed" || sdStatus === "Error")
    return { label: "Critical", tone: "bad" as const };
  if (sdStatus === "Read Only") return { label: "Read only", tone: "warn" as const };
  return { label: sdStatus || "Unknown", tone: "unknown" as const };
}

export default function DashboardPage() {
  const queryClient = useQueryClient();
  const outlet = useOutletContext<AdminOutletContext | undefined>();
  const isOwner = outlet?.isOwner ?? true;
  const permissions = outlet?.permissions ?? DEFAULT_OPERATOR_PERMISSIONS;
  const { fallbackPollMs, liveUpdatesEnabled, setLiveUpdatesEnabled } = useRealtime();
  const [detailsEnabled, setDetailsEnabled] = useState(false);
  const [salesChartEnabled, setSalesChartEnabled] = useState(false);
  const [salesPeriod, setSalesPeriod] = useState<SalesPeriod>("today");
  const {
    data: status,
    isLoading: statusLoading,
    isError: statusError,
    error: statusErr,
    refetch: refetchStatus,
    isFetching: statusFetching,
    dataUpdatedAt: statusDataUpdatedAt,
  } = useSystemStatus();
  const {
    data: accessPointList,
    isLoading: accessPointsLoading,
    isError: accessPointsError,
  } = useQuery({
    queryKey: ACCESS_POINTS_QUERY_KEY,
    queryFn: () => accessPointsApi.list(),
    enabled: isOwner,
    staleTime: Number.POSITIVE_INFINITY,
    refetchOnMount: false,
  });
  const {
    data: contentFilter,
    isLoading: contentFilterLoading,
    isError: contentFilterError,
  } = useQuery({
    queryKey: CONTENT_FILTER_QUERY_KEY,
    queryFn: () => contentFilterApi.get(),
    enabled: isOwner,
    staleTime: 30_000,
  });
  const secondaryQueriesEnabled = detailsEnabled && !statusLoading && status !== undefined;
  const {
    data: coinDiagnostics,
    isLoading: coinDiagLoading,
    isError: coinDiagError,
  } = useQuery({
    queryKey: ["coin", "diagnostics"],
    queryFn: () => coinApi.diagnostics(),
    enabled: secondaryQueriesEnabled,
    refetchInterval: liveUpdatesEnabled ? fallbackPollMs : false,
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
    enabled: secondaryQueriesEnabled,
    refetchInterval: liveUpdatesEnabled ? fallbackPollMs : false,
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
    enabled: secondaryQueriesEnabled,
    refetchInterval: liveUpdatesEnabled ? fallbackPollMs : false,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });
  const { data: salesChart, isFetching: chartFetching } = useQuery({
    queryKey: ["sales", "chart", salesPeriod],
    queryFn: () => {
      if (salesPeriod === "weekly") return salesApi.chartWeekly();
      if (salesPeriod === "monthly") return salesApi.chartMonthly();
      return salesApi.chartDaily();
    },
    enabled: salesChartEnabled,
    staleTime: 30_000,
    refetchIntervalInBackground: false,
  });

  const reloadSalesMutation = useMutation({
    mutationFn: async () => {
      setSalesChartEnabled(true);
      setDetailsEnabled(true);
      await refetchStatus();
      await queryClient.invalidateQueries({ queryKey: ["sales"] });
      await queryClient.invalidateQueries({ queryKey: ["sales", "chart"] });
      const sdMissing =
        status?.storage?.sd?.present === false ||
        status?.storageStatus?.mounted === false ||
        status?.storage?.sd?.status === "Missing";
      if (sdMissing) {
        try {
          await systemApi.retrySd();
        } catch {
          // Optional promote/retry — Core status refresh still succeeds from SPIFFS.
        }
      }
    },
    onSuccess: () => {
      toast.success("Sales refreshed from appliance storage.");
    },
    onError: (err) => {
      toast.error(err instanceof Error ? err.message : "Unable to reload sales.");
    },
  });

  const coinLoading = statusLoading || (detailsEnabled && (coinDiagLoading || coinSystemLoading));
  const today = status?.sales?.today;
  const weekly = status?.sales?.weekly;
  const monthly = status?.sales?.monthly;
  const storage = status?.storage;
  const sd = storage?.sd;

  const mikrotik = mikrotikDisplay(status?.mikrotik, statusLoading);
  const routerCache = status?.routerCache;
  const routerOs = routerCache?.routerOs;
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
  const wan = wanStatusDisplay(status?.internet, statusLoading, status?.wan);
  const coin = coinDisplay(
    status,
    coinDiagnostics?.stats?.state,
    coinDiagnostics?.stats?.total_today,
    coinLoading,
  );

  const flashPct = usagePct(storage?.flashUsedMb ?? 0, storage?.flashTotalMb ?? 0);
  const logsPct = usagePct(storage?.logsUsedKb ?? 0, storage?.logsTotalKb ?? 0);
  const sdPct = usagePct(sd?.usedMb ?? 0, sd?.totalMb ?? 0);
  const sdReady = sd?.mounted === true && sd?.status === "Ready";
  const sdFreePct = sdReady && sd && sd.totalMb > 0 ? (sd.freeMb / sd.totalMb) * 100 : undefined;

  const pausedUsers = status?.activeUsers?.paused;
  const activeSessionCount = status?.activeUsers?.count;
  const sessionHistory = useRollingMetricHistory(
    statusLoading ? undefined : activeSessionCount,
    24,
    statusDataUpdatedAt,
  );

  const loadErrors: string[] = [];
  if (statusError) loadErrors.push("Unable to load data from /api/status");
  if (detailsEnabled && healthError) {
    loadErrors.push("Unable to load data from /api/system/health");
  }
  if (detailsEnabled && coinSystemError) {
    loadErrors.push("Unable to load data from /api/system/coin");
  }
  if (detailsEnabled && coinDiagError) {
    loadErrors.push("Unable to load coin diagnostics");
  }

  if (statusErr) {
    console.warn("[dashboard] status query error detail", statusErr);
  }

  const routerOsVersion = statusLoading
    ? "Loading..."
    : routerOs?.version || routerCache?.routerOsVersion || "N/A";

  const routerCacheStale = isRouterCacheStale(routerCache);
  const controllerOnline = Boolean(status?.server?.ok) && Boolean(status);
  const internetRowTone: StatusTone = wan.variant === "unknown" ? "unknown" : wan.ok ? "ok" : "bad";
  const mikrotikConnectionLabel = statusLoading
    ? "Loading..."
    : !mikrotik.ok
      ? "Not configured"
      : mikrotik.connectivityOk
        ? "Connected"
        : mikrotik.connectivityLabel === "Offline"
          ? "Disconnected"
          : mikrotik.connectivityLabel;
  const mikrotikConnectionTone = connectivityTone(
    mikrotik.connectivityOk,
    mikrotik.connectivityLabel,
  );
  const controllerLabel = statusLoading
    ? "Loading..."
    : controllerOnline
      ? "Connected"
      : "Disconnected";
  const accessPointRow = isOwner
    ? accessPointRegistryDisplay(
        accessPointList?.accessPoints,
        accessPointsLoading,
        accessPointsError,
      )
    : { label: "Unknown", tone: "unknown" as const };
  const internetStatusLabel = statusLoading
    ? "Loading..."
    : wan.show && wan.ok
      ? "Online"
      : wan.show
        ? wan.label
        : "Unknown";
  const networkStatusRows: NetworkStatusRow[] = [
    {
      label: "Internet",
      value: internetStatusLabel,
      tone: internetRowTone,
    },
    {
      label: "MikroTik",
      value: mikrotikConnectionLabel,
      tone: mikrotikConnectionTone,
    },
    {
      label: "KonekSik-fi Controller",
      value: controllerLabel,
      tone: controllerOnline ? "ok" : statusLoading ? "unknown" : "bad",
    },
    {
      label: "Access Point",
      value: accessPointRow.label,
      tone: accessPointRow.tone,
    },
  ];
  const routerHdd = formatRouterHddOverview(routerOs?.freeHddSpace, routerOs?.totalHddSpace);
  const routerTemperature = formatRouterTemperature(routerOs?.cpuTemperature);
  const hasRouterStorage = routerHdd.total !== "N/A";
  const mikrotikSnapshot: MikrotikRouterSnapshot = {
    model: statusLoading
      ? "Loading..."
      : routerOs?.boardName?.trim() || routerCache?.identity?.trim() || "N/A",
    routerOs: routerOsVersion,
    uptime: statusLoading ? "Loading..." : routerOs?.uptime || "N/A",
    connection: mikrotikConnectionLabel,
    connectionTone: mikrotikConnectionTone,
    cpu: statusLoading ? "Loading..." : routerOs?.cpuLoad ? `${routerOs.cpuLoad}%` : "N/A",
    memory: statusLoading
      ? "Loading..."
      : formatRouterMemory(routerOs?.freeMemory, routerOs?.totalMemory),
    storage: {
      total: routerHdd.total,
      used: routerHdd.used,
      available: routerHdd.available,
      usagePctLabel: routerHdd.usagePct,
      usagePctValue: routerHdd.usagePctValue,
      hasData: hasRouterStorage,
    },
    temperature: statusLoading ? "Loading..." : routerTemperature,
    lastSyncLabel: routerCache?.populated ? routerCacheLastSyncLabel(routerCache) : undefined,
  };

  const sdInfo = sdBadge(status?.storageStatus?.health, sd?.status, sd?.present);
  const cpuMhz = systemHealth?.esp32?.cpuFreqMHz;
  const cpuPct = cpuMhz != null ? clampPct((cpuMhz / Math.max(240, cpuMhz)) * 100) : 0;
  const tempC = systemHealth?.esp32?.chipTempC;
  const tempAvailable = Boolean(systemHealth?.esp32?.chipTempAvailable && tempC != null);
  const heapFreeKb =
    storage?.internalHeap?.freeKb ??
    (systemHealth?.memory?.heap !== undefined
      ? Math.round(systemHealth.memory.heap / 1024)
      : undefined);
  const heapTotalKb = storage?.internalHeap?.totalKb;
  const heapPct = heapFreeKb !== undefined && heapTotalKb ? usagePct(heapFreeKb, heapTotalKb) : 0;
  const psramFreeKb =
    storage?.psram?.freeKb ??
    (systemHealth?.memory?.psram !== undefined
      ? Math.round(systemHealth.memory.psram / 1024)
      : undefined);
  const psramTotalKb =
    storage?.psram?.totalKb ??
    (systemHealth?.memory?.psramSize
      ? Math.round(systemHealth.memory.psramSize / 1024)
      : undefined);
  const psramPct =
    psramFreeKb !== undefined && psramTotalKb ? usagePct(psramFreeKb, psramTotalKb) : 0;

  const totalCoins = coinSystem?.totalCoinCount ?? status?.coinSlot?.totalCoinCount;
  const coinsToday = coin.pulsesToday;
  const lastCoin =
    coinSystem?.lastCoinTimestamp ||
    coinSystem?.lastPulseTimestamp ||
    status?.coinSlot?.lastCoinTimestamp ||
    status?.coinSlot?.lastPulseTimestamp;
  const retryStatus = () => {
    void queryClient.invalidateQueries({ queryKey: ["system", "status"] });
  };

  return (
    <div className="space-y-4">
      <div className="page-enter">
        <h2 className="text-2xl font-semibold leading-tight">Dashboard</h2>
        <p className="mt-0.5 text-[13px] text-muted-foreground">Overview of your Renz-Fi system</p>
      </div>

      <div
        className="page-enter flex flex-col gap-2 rounded-md border border-border/70 bg-muted/30 px-3 py-3 sm:flex-row sm:flex-wrap sm:items-center sm:justify-between"
        style={{ animationDelay: "30ms" }}
      >
        <p className="text-sm text-muted-foreground">
          Standby mode: Connect loads Core status only. Use the buttons for RouterOS, sales chart,
          or live EventSource updates.
        </p>
        <div className="flex flex-wrap gap-2">
          <Button
            type="button"
            size="sm"
            variant="secondary"
            disabled={reloadSalesMutation.isPending || statusFetching || chartFetching}
            onClick={() => reloadSalesMutation.mutate()}
          >
            {reloadSalesMutation.isPending ? "Reloading…" : "Reload Sales"}
          </Button>
          <Button
            type="button"
            size="sm"
            variant="secondary"
            disabled={syncRouterMutation.isPending || storageRecovering}
            onClick={() => syncRouterMutation.mutate()}
          >
            {syncRouterMutation.isPending ? "Synchronizing…" : "Synchronize Router"}
          </Button>
          <Button
            type="button"
            size="sm"
            variant={liveUpdatesEnabled ? "default" : "outline"}
            onClick={() => {
              const next = !liveUpdatesEnabled;
              setLiveUpdatesEnabled(next);
              if (next) setDetailsEnabled(true);
              toast.message(
                next ? "Live updates on (SSE + refresh)." : "Live updates off — Admin standby.",
              );
            }}
          >
            {liveUpdatesEnabled ? "Live updates: On" : "Live updates: Off"}
          </Button>
          {!detailsEnabled ? (
            <Button
              type="button"
              size="sm"
              variant="outline"
              onClick={() => setDetailsEnabled(true)}
            >
              Load coin / health details
            </Button>
          ) : null}
        </div>
      </div>

      {loadErrors.length > 0 ? (
        <div className="page-enter" style={{ animationDelay: "40ms" }}>
          <DashboardLoadError
            message={`${loadErrors.join(". ")}. Showing the last known values where available.`}
          />
        </div>
      ) : null}

      <div className="page-enter" style={{ animationDelay: "60ms" }}>
        <RouterCacheStaleBanner
          cache={routerCache}
          pending={syncRouterMutation.isPending || storageRecovering}
          onRefresh={() => syncRouterMutation.mutate()}
        />
      </div>

      <section className="page-enter space-y-3" style={{ animationDelay: "90ms" }}>
        <h3 className="text-lg font-semibold tracking-tight text-foreground">Sales</h3>
        <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 xl:grid-cols-4">
          <MonthlySalesCard
            loading={statusLoading}
            period={salesPeriod}
            onPeriodChange={setSalesPeriod}
            todayAmount={today?.amount}
            todaySessions={today?.sessions}
            weekAmount={weekly?.amount}
            weekSessions={weekly?.sessions}
            monthAmount={monthly?.amount}
            monthSessions={monthly?.sessions}
            chartValues={salesChart?.data ?? []}
          />
          <ActiveUsersCard
            loading={statusLoading}
            count={status?.activeUsers?.count}
            paused={pausedUsers}
          />
          <CoinSummaryCard
            loading={coinLoading}
            coinsToday={coinsToday}
            lastCoin={lastCoin}
            hardwareState={coinSystem?.state ?? coin.hardwareState}
          />
          <TotalCoinsCard
            loading={coinLoading}
            totalCoins={totalCoins}
            monthlySessions={monthly?.sessions}
          />
        </div>
      </section>

      <section className="page-enter space-y-2" style={{ animationDelay: "160ms" }}>
        <h3 className="text-sm font-semibold uppercase tracking-[0.12em] text-muted-foreground">
          Quick Actions
        </h3>
        <QuickActionsBar isOwner={isOwner} permissions={permissions} />
      </section>

      <div
        className="page-enter grid grid-cols-1 gap-3 xl:grid-cols-2"
        style={{ animationDelay: "230ms" }}
      >
        <SdCardHealthCard
          loading={statusLoading}
          error={statusError}
          onRetry={retryStatus}
          badgeLabel={sdInfo.label}
          badgeTone={sdInfo.tone}
          freePct={sdFreePct}
          capacityLabel={sdReady ? formatStorageFromMb(sd?.totalMb) : "N/A"}
          uptimeLabel={status?.esp32?.uptime ?? "N/A"}
          mounted={sdReady}
          flashUsed={storage?.flashUsedMb}
          flashTotal={storage?.flashTotalMb}
          flashPct={flashPct}
          logsUsed={storage?.logsUsedKb}
          logsTotal={storage?.logsTotalKb}
          logsPct={logsPct}
          sdReady={sdReady}
          sdUsed={sd?.usedMb}
          sdTotal={sd?.totalMb}
          sdFree={sd?.freeMb}
          sdPct={sdPct}
          sdStatus={statusLoading ? "Loading..." : (sd?.status ?? "N/A")}
        />
        <MikrotikRouterCard
          loading={statusLoading}
          error={statusError}
          onRetry={retryStatus}
          snapshot={mikrotikSnapshot}
          stale={routerCacheStale}
          showStorageSyncHint={!hasRouterStorage && !statusLoading}
        />
        <SystemHealthCard
          loading={healthLoading}
          error={healthError}
          onRetry={() => {
            void queryClient.invalidateQueries({ queryKey: ["system", "health"] });
          }}
          level={systemHealth?.level}
          cpuMhz={cpuMhz}
          cpuPct={cpuPct}
          tempC={tempC}
          tempPct={tempAvailable && tempC != null ? clampPct((tempC / 80) * 100) : 0}
          tempAvailable={tempAvailable}
          heapLabel={heapFreeKb === undefined ? "N/A" : formatKb(heapFreeKb)}
          heapPct={heapPct}
          psramLabel={psramFreeKb === undefined ? "N/A" : formatKb(psramFreeKb)}
          psramPct={psramPct}
          ethernetDriver={healthLoading ? "Loading..." : (systemHealth?.ethernet?.driver ?? "N/A")}
          ethernetLink={healthLoading ? "Loading..." : (systemHealth?.ethernet?.link ?? "N/A")}
        />
        <ContentFilteringCard
          loading={contentFilterLoading}
          error={contentFilterError}
          onRetry={() => {
            void queryClient.invalidateQueries({ queryKey: CONTENT_FILTER_QUERY_KEY });
          }}
          state={contentFilter}
        />
      </div>

      <div className="page-enter" style={{ animationDelay: "300ms" }}>
        <NetworkStatusCard
          loading={statusLoading}
          error={statusError}
          onRetry={retryStatus}
          rows={networkStatusRows}
          stale={routerCacheStale}
          sessionHistory={sessionHistory}
          sessionCount={activeSessionCount}
        />
      </div>
    </div>
  );
}
