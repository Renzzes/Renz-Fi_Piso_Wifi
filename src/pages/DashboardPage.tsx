import { useOutletContext } from "react-router-dom";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { RouterCacheStaleBanner } from "@/components/RouterCacheStaleBanner";
import type { AdminOutletContext } from "@/components/AdminLayout";
import {
  ActiveUsersCard,
  CoinSlotStatusCard,
  CoinSummaryCard,
  ConnectivityStatusRow,
  MonthlySalesCard,
  QuickActionsCard,
  SdCardHealthCard,
  StorageUsageCard,
  SystemHealthCard,
  SystemStatusCard,
  TotalCoinsCard,
} from "@/components/dashboard";
import type { DashboardStatusRow } from "@/components/dashboard/MonitoringCards";
import { useSystemStatus } from "@/hooks/api/useSystemStatus";
import { useRealtime } from "@/contexts/RealtimeContext";
import { coinApi } from "@/services/coin";
import { routerApi } from "@/services/router";
import { salesApi } from "@/services/sales";
import { healthApi } from "@/services/rgb";
import { routerCacheLastSyncLabel } from "@/lib/routerCacheStatus";
import {
  isProductionNetworkHealthy,
  productionNetworkReasonLabel,
} from "@/lib/productionNetworkReason";
import { refreshProductionRouterViews } from "@/lib/refreshProductionRouterViews";
import { adminConnectionStatusDisplay, wanStatusDisplay } from "@/lib/adminStatus";
import {
  boolStatus,
  coinDisplay,
  coinHardwareTone,
  coinRateLabel,
  connectivityTone,
  hotspotDisplay,
  mikrotikDisplay,
} from "@/lib/dashboardDisplay";
import {
  clampPct,
  formatKb,
  formatRouterMemory,
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
  const { fallbackPollMs, adminApiReachable } = useRealtime();
  const {
    data: status,
    isLoading: statusLoading,
    isError: statusError,
    error: statusErr,
  } = useSystemStatus();
  const secondaryQueriesEnabled = !statusLoading && status !== undefined;
  const {
    data: coinDiagnostics,
    isLoading: coinDiagLoading,
    isError: coinDiagError,
  } = useQuery({
    queryKey: ["coin", "diagnostics"],
    queryFn: () => coinApi.diagnostics(),
    enabled: secondaryQueriesEnabled,
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
    enabled: secondaryQueriesEnabled,
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
    enabled: secondaryQueriesEnabled,
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });
  const { data: coinSettings, isLoading: coinSettingsLoading } = useQuery({
    queryKey: ["coin", "settings"],
    queryFn: () => coinApi.settings(),
    enabled: secondaryQueriesEnabled,
    staleTime: 30_000,
    refetchIntervalInBackground: false,
  });
  const { data: dailyChart } = useQuery({
    queryKey: ["sales", "chart", "daily"],
    queryFn: () => salesApi.chartDaily(),
    enabled: secondaryQueriesEnabled,
    staleTime: 30_000,
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
    mikrotik.ok ? `Online • Identity: ${routerCache?.identity || mikrotik.host}` : mikrotik.host,
    routerCache?.populated ? `Last sync: ${routerCacheLastSyncLabel(routerCache)}` : null,
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
  const logsPct = usagePct(storage?.logsUsedKb ?? 0, storage?.logsTotalKb ?? 0);
  const sdPct = usagePct(sd?.usedMb ?? 0, sd?.totalMb ?? 0);
  const sdReady = sd?.mounted === true && sd?.status === "Ready";
  const sdFreePct = sdReady && sd && sd.totalMb > 0 ? (sd.freeMb / sd.totalMb) * 100 : undefined;

  const database = boolStatus(status?.database?.ok, statusLoading, "Healthy", "Error");
  const server = boolStatus(status?.server?.ok, statusLoading, "Running", "Down");

  const uptimeStatus = statusLoading ? "Loading..." : (status?.esp32?.uptime ?? "N/A");
  const syncStatus = statusLoading
    ? "Loading..."
    : status?.sync?.pending !== undefined
      ? `${status.sync.pending} items`
      : "N/A";

  const pausedUsers = status?.activeUsers?.paused;
  const pausedUsersStatus = statusLoading
    ? "Loading..."
    : pausedUsers !== undefined
      ? String(pausedUsers)
      : "N/A";

  const loadErrors: string[] = [];
  if (statusError) loadErrors.push("Unable to load data from /api/status");
  if (healthError) loadErrors.push("Unable to load data from /api/system/health");
  if (coinSystemError) loadErrors.push("Unable to load data from /api/system/coin");
  if (coinDiagError) loadErrors.push("Unable to load coin diagnostics");

  if (statusErr) {
    console.warn("[dashboard] status query error detail", statusErr);
  }

  const wanTone = wan.variant === "unknown" ? "unknown" : wan.ok ? "ok" : "bad";
  const productionSsid = statusLoading
    ? "Loading..."
    : routerCache?.productionNetwork?.ssid || routerCache?.ssid || "N/A";
  const routerOsVersion = statusLoading
    ? "Loading..."
    : routerOs?.version || routerCache?.routerOsVersion || "N/A";

  const primaryStatusRows: DashboardStatusRow[] = [
    { label: "MikroTik Router", value: mikrotik.label, tone: mikrotik.ok ? "ok" : "bad" },
    {
      label: "Connectivity",
      value: mikrotik.connectivityLabel,
      tone: connectivityTone(mikrotik.connectivityOk, mikrotik.connectivityLabel),
    },
    {
      label: "RouterOS Version",
      value: routerOsVersion,
      tone: routerOsVersion !== "N/A" && routerOsVersion !== "Loading..." ? "ok" : "unknown",
      mono: true,
    },
    {
      label: "Production SSID",
      value: productionSsid,
      tone: productionSsid !== "N/A" && productionSsid !== "Loading..." ? "ok" : "unknown",
    },
    ...(wan.show
      ? [{ label: "WAN Internet", value: wan.label, tone: wanTone as DashboardStatusRow["tone"] }]
      : []),
    {
      label: "Coin Hardware",
      value: coin.hardwareLabel,
      tone: coinHardwareTone(coin.hardwareState),
    },
    {
      label: "ESP32",
      value: statusLoading && !status ? "Loading..." : status ? "Online" : "Offline",
      tone: status ? "ok" : statusLoading ? "unknown" : "bad",
    },
    {
      label: "ESP32 Uptime",
      value: uptimeStatus,
      tone: status ? "ok" : statusLoading ? "unknown" : "bad",
      mono: true,
    },
  ];

  const extraStatusRows: DashboardStatusRow[] = [
    {
      label: "Router Identity",
      value: statusLoading ? "Loading..." : routerCache?.identity || "N/A",
      tone: routerCache?.identity ? "ok" : "unknown",
    },
    {
      label: "CPU",
      value: statusLoading ? "Loading..." : routerOs?.cpuLoad ? `${routerOs.cpuLoad}%` : "N/A",
      tone: routerOs?.cpuLoad ? "ok" : "unknown",
      mono: true,
    },
    {
      label: "Memory",
      value: statusLoading
        ? "Loading..."
        : formatRouterMemory(routerOs?.freeMemory, routerOs?.totalMemory),
      tone:
        formatRouterMemory(routerOs?.freeMemory, routerOs?.totalMemory) !== "N/A"
          ? "ok"
          : "unknown",
      mono: true,
    },
    {
      label: "Router Uptime",
      value: statusLoading ? "Loading..." : routerOs?.uptime || "N/A",
      tone: routerOs?.uptime ? "ok" : "unknown",
      mono: true,
    },
    { label: "Hotspot", value: hotspot.label, tone: hotspot.ok ? "ok" : "unknown" },
    {
      label: "Admin Connection",
      value: adminConnection.label,
      tone: adminConnection.ok ? "ok" : "bad",
    },
    { label: "Coin Slot Feature", value: coin.featureLabel, tone: coin.ok ? "ok" : "neutral" },
    {
      label: "Paused Users",
      value: pausedUsersStatus,
      tone: pausedUsers === 0 ? "ok" : pausedUsers ? "warn" : "unknown",
    },
    {
      label: "Pending Sync",
      value: syncStatus,
      tone: status?.sync?.pending === 0 ? "ok" : "unknown",
    },
    { label: "Database", value: database.label, tone: database.ok ? "ok" : "bad" },
    { label: "API Server", value: server.label, tone: server.ok ? "ok" : "bad" },
    ...(routerCache?.populated && routerCache.productionNetwork
      ? [
          {
            label: "Production Wi-Fi",
            value: isProductionNetworkHealthy(routerCache.productionNetwork)
              ? "Healthy"
              : productionNetworkReasonLabel(routerCache.productionNetwork.reason),
            tone: (isProductionNetworkHealthy(routerCache.productionNetwork)
              ? "ok"
              : "warn") as DashboardStatusRow["tone"],
          },
        ]
      : []),
  ];

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

      <div
        className="page-enter grid grid-cols-1 gap-3 sm:grid-cols-2 xl:grid-cols-4"
        style={{ animationDelay: "90ms" }}
      >
        <MonthlySalesCard
          loading={statusLoading}
          amount={monthly?.amount}
          sessions={monthly?.sessions}
          todayAmount={today?.amount}
          weekAmount={weekly?.amount}
          chartValues={dailyChart?.data ?? []}
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

      <div
        className="page-enter grid grid-cols-1 gap-3 xl:grid-cols-3"
        style={{ animationDelay: "160ms" }}
      >
        <SystemStatusCard
          loading={statusLoading}
          error={statusError}
          onRetry={retryStatus}
          primary={primaryStatusRows}
          extra={extraStatusRows}
        />
        <CoinSlotStatusCard
          loading={coinLoading}
          error={coinSystemError || coinDiagError}
          onRetry={() => {
            void queryClient.invalidateQueries({ queryKey: ["system", "coin"] });
            void queryClient.invalidateQueries({ queryKey: ["coin", "diagnostics"] });
          }}
          enabled={Boolean(
            coinSystem?.enabled ?? status?.coinSlot?.enabled ?? status?.coinSlot?.ok,
          )}
          featureLabel={coin.featureLabel}
          hardwareLabel={coin.hardwareLabel}
          hardwareState={coinSystem?.state ?? coin.hardwareState}
          coinsToday={coinsToday}
          totalCoins={
            coinSystemLoading
              ? "…"
              : coinSystem?.totalCoinCount !== undefined
                ? String(coinSystem.totalCoinCount)
                : coin.totalCoins
          }
          lastCoin={lastCoin}
          rateLabel={coinRateLabel(coinSettings, coinSettingsLoading)}
        />
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
        />
      </div>

      <div
        className="page-enter grid grid-cols-1 gap-3 xl:grid-cols-3"
        style={{ animationDelay: "230ms" }}
      >
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
        <StorageUsageCard
          loading={statusLoading}
          error={statusError}
          onRetry={retryStatus}
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
          heapUsed={storage?.internalHeap?.usedKb ?? storage?.ramUsedKb}
          heapTotal={storage?.internalHeap?.totalKb ?? storage?.ramTotalKb}
          psramPresent={Boolean(storage?.psram?.present)}
          psramUsed={storage?.psram?.usedKb}
          psramTotal={storage?.psram?.totalKb}
          dmaFree={storage?.dma?.freeKb}
          dmaLargest={storage?.dma?.largestKb}
        />
        <QuickActionsCard isOwner={isOwner} permissions={permissions} />
      </div>

      <div className="page-enter" style={{ animationDelay: "300ms" }}>
        <ConnectivityStatusRow
          mikrotikLabel={mikrotik.label}
          mikrotikTone={mikrotik.ok ? "ok" : "bad"}
          mikrotikHint={mikrotikHint}
          adminLabel={adminConnection.label}
          adminTone={adminConnection.ok ? "ok" : "bad"}
          wanLabel={wan.label}
          wanTone={wanTone}
          wanHint={wan.latency}
          showWan={wan.show}
          coinLabel={coin.hardwareLabel}
          coinTone={coinHardwareTone(coinSystem?.state ?? coin.hardwareState)}
          coinHint={coin.pulses}
          hotspotLabel={hotspot.label}
          hotspotTone={hotspot.ok ? "ok" : "unknown"}
        />
      </div>
    </div>
  );
}
