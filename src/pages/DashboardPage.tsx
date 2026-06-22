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
import { useQuery } from "@tanstack/react-query";
import { StatCard, StatusRow } from "@/components/StatCard";
import { PageHeader } from "@/components/PageHeader";
import { Progress } from "@/components/ui/progress";
import { useSystemStatus } from "@/hooks/api/useSystemStatus";
import { useRealtime } from "@/contexts/RealtimeContext";
import { coinApi } from "@/services/coin";
import { formatPeso } from "@/lib/currency";
import { adminConnectionStatusDisplay, wanStatusDisplay } from "@/lib/adminStatus";
import type { SystemStatus } from "@/types/api";

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

function mikrotikDisplay(
  mikrotik: SystemStatus["mikrotik"] | undefined,
  loading: boolean,
): { label: string; ok: boolean; host: string } {
  if (loading) return { label: "Loading...", ok: false, host: "Loading..." };
  if (!mikrotik) return { label: "—", ok: false, host: "—" };

  const host = mikrotik.host?.trim() ?? "";
  if (!host) return { label: "Not Configured", ok: false, host: "Not Configured" };

  return {
    label: mikrotik.ok ? "Configured" : "Unavailable",
    ok: mikrotik.ok,
    host,
  };
}

function hotspotDisplay(
  hotspot: SystemStatus["hotspot"] | undefined,
  loading: boolean,
): { label: string; ok: boolean; ssid: string } {
  if (loading) return { label: "Loading...", ok: false, ssid: "Loading..." };
  if (!hotspot) return { label: "—", ok: false, ssid: "—" };

  const ssid = hotspot.ssid?.trim() ?? "";
  if (!ssid) return { label: "Not Configured", ok: false, ssid: "Not Configured" };

  return {
    label: hotspot.ok ? "Online" : "Offline",
    ok: hotspot.ok,
    ssid,
  };
}

function coinDisplay(
  status: SystemStatus | undefined,
  diagState: string | undefined,
  diagPulses: string | undefined,
  loading: boolean,
): { label: string; ok: boolean; pulses: string } {
  if (loading) return { label: "Loading...", ok: false, pulses: "Loading..." };

  const state = diagState ?? status?.coinSlot.state;
  const pulses =
    diagPulses ??
    (status?.coinSlot.pulsesToday !== undefined ? String(status.coinSlot.pulsesToday) : undefined);

  return {
    label: state ?? "—",
    ok: status?.coinSlot.ok ?? false,
    pulses: pulses !== undefined ? `${pulses} pulses` : "—",
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

export default function DashboardPage() {
  const { fallbackPollMs, adminApiReachable } = useRealtime();
  const { data: status, isLoading: statusLoading } = useSystemStatus();
  const { data: coinDiagnostics, isLoading: coinDiagLoading } = useQuery({
    queryKey: ["coin", "diagnostics"],
    queryFn: () => coinApi.diagnostics(),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });

  const coinLoading = statusLoading || coinDiagLoading;
  const today = status?.sales.today;
  const weekly = status?.sales.weekly;
  const monthly = status?.sales.monthly;
  const storage = status?.storage;
  const sd = storage?.sd;

  const mikrotik = mikrotikDisplay(status?.mikrotik, statusLoading);
  const hotspot = hotspotDisplay(status?.hotspot, statusLoading);
  const adminConnection = adminConnectionStatusDisplay(adminApiReachable, statusLoading);
  const wan = wanStatusDisplay(status?.internet, statusLoading);
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

  const database = boolStatus(status?.database.ok, statusLoading, "Healthy", "Error");
  const server = boolStatus(status?.server.ok, statusLoading, "Running", "Down");

  const activeCount = statusLoading
    ? "Loading..."
    : status?.activeUsers.count !== undefined
      ? status.activeUsers.count
      : "—";

  const pausedUsers = status?.activeUsers.paused;
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

  const uptimeStatus = statusLoading ? "Loading..." : (status?.esp32.uptime ?? "—");

  const syncStatus = statusLoading
    ? "Loading..."
    : status?.sync.pending !== undefined
      ? `${status.sync.pending} items`
      : "—";

  const wanTone =
    wan.variant === "unknown" ? "default" : wan.ok ? "success" : "danger";

  const statusCardCount = wan.show ? 5 : 4;

  return (
    <div>
      <PageHeader title="Dashboard" description="Overview of your Renz-Fi system" />

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

      <div className="grid md:grid-cols-2 gap-3 mt-3">
        <div className="rounded-md border bg-card p-3">
          <div className="text-sm font-medium mb-2 flex items-center gap-2">
            <Activity className="h-4 w-4" /> System Status
          </div>
          <StatusRow label="MikroTik Router" status={mikrotik.label} ok={mikrotik.ok} />
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
          <StatusRow label="Coin Slot" status={coin.label} ok={coin.ok} />
          <StatusRow label="Paused Users" status={pausedUsersStatus} ok={pausedUsers === 0} />
          <StatusRow
            label="ESP32 Uptime"
            status={uptimeStatus}
            ok={Boolean(status?.esp32.lastSeen)}
          />
          <StatusRow
            label="Pending Sync"
            status={syncStatus}
            ok={status?.sync.pending !== undefined && status.sync.pending === 0}
          />
          <StatusRow label="Database" status={database.label} ok={database.ok} />
          <StatusRow label="API Server" status={server.label} ok={server.ok} />
        </div>

        <div className="rounded-md border bg-card p-3">
          <div className="text-sm font-medium mb-2 flex items-center gap-2">
            <HardDrive className="h-4 w-4" /> Storage Usage
          </div>
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
                <span className="text-muted-foreground">RAM</span>
                <span className="tabular-nums">
                  {statusLoading
                    ? "Loading..."
                    : storage
                      ? `${storage.ramUsedKb} / ${storage.ramTotalKb} KB`
                      : "—"}
                </span>
              </div>
              <Progress value={ramPct} className="h-1.5" />
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
        </div>
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
          hint={mikrotik.host}
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
          value={coin.label}
          icon={Coins}
          tone={coin.ok ? "success" : "default"}
          hint={coin.pulses}
        />
        <StatCard
          label="Hotspot"
          value={hotspot.label}
          icon={Wifi}
          tone={hotspot.ok ? "success" : "danger"}
          hint={hotspot.ssid === "Not Configured" ? hotspot.ssid : `SSID: ${hotspot.ssid}`}
        />
      </div>
    </div>
  );
}
