import { Activity, DollarSign, HardDrive, Users, Wifi, Coins, Router, Globe } from "lucide-react";
import { StatCard, StatusRow } from "@/components/StatCard";
import { PageHeader } from "@/components/PageHeader";
import { Progress } from "@/components/ui/progress";
import { useSystemStatus } from "@/hooks/api/useSystemStatus";

export default function DashboardPage() {
  const { data: status } = useSystemStatus();

  const today = status?.sales.today;
  const weekly = status?.sales.weekly;
  const monthly = status?.sales.monthly;
  const storage = status?.storage;
  const flashPct = storage ? Math.round((storage.flashUsedMb / storage.flashTotalMb) * 100) : 40;
  const ramPct = storage ? Math.round((storage.ramUsedKb / storage.ramTotalKb) * 100) : 40;
  const logsPct = storage ? Math.round((storage.logsUsedKb / storage.logsTotalKb) * 100) : 32;

  return (
    <div>
      <PageHeader title="Dashboard" description="Overview of your Renz-Fi system" />

      <div className="grid grid-cols-2 md:grid-cols-4 gap-2 sm:gap-3">
        <StatCard
          label="Today's Sales"
          value={`₱ ${today?.amount ?? 248}`}
          icon={DollarSign}
          tone="success"
          hint={`${today?.sessions ?? 32} sessions`}
        />
        <StatCard
          label="Weekly Sales"
          value={`₱ ${weekly?.amount ?? 1820}`}
          icon={DollarSign}
          hint={`${weekly?.sessions ?? 208} sessions`}
        />
        <StatCard
          label="Monthly Sales"
          value={`₱ ${monthly?.amount ?? 7415}`}
          icon={DollarSign}
          hint={`${monthly?.sessions ?? 842} sessions`}
        />
        <StatCard
          label="Active Users"
          value={status?.activeUsers.count ?? 12}
          icon={Users}
          tone="success"
          hint={`${status?.activeUsers.idle ?? 2} idle`}
        />
      </div>

      <div className="grid md:grid-cols-2 gap-3 mt-3">
        <div className="rounded-md border bg-card p-3">
          <div className="text-sm font-medium mb-2 flex items-center gap-2">
            <Activity className="h-4 w-4" /> System Status
          </div>
          <StatusRow
            label="MikroTik Router"
            status={status?.mikrotik.ok ? "Connected" : "Disconnected"}
            ok={status?.mikrotik.ok ?? true}
          />
          <StatusRow
            label="Internet"
            status={status?.internet.ok ? "Connected" : "Disconnected"}
            ok={status?.internet.ok ?? true}
          />
          <StatusRow
            label="Coin Slot"
            status={status?.coinSlot.state ?? "Ready"}
            ok={status?.coinSlot.ok ?? true}
          />
          <StatusRow
            label="ESP32 Uptime"
            status={status?.esp32.uptime ?? "3d 4h 12m"}
            ok={Boolean(status?.esp32.lastSeen)}
          />
          <StatusRow
            label="Pending Sync"
            status={`${status?.sync.pending ?? 0} items`}
            ok={(status?.sync.pending ?? 0) === 0}
          />
          <StatusRow
            label="Database"
            status={status?.database.ok ? "Healthy" : "Error"}
            ok={status?.database.ok ?? true}
          />
          <StatusRow
            label="API Server"
            status={status?.server.ok ? "Running" : "Down"}
            ok={status?.server.ok ?? true}
          />
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
                  {storage?.flashUsedMb ?? 1.2} / {storage?.flashTotalMb ?? 3.0} MB
                </span>
              </div>
              <Progress value={flashPct} className="h-1.5" />
            </div>
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground">RAM</span>
                <span className="tabular-nums">
                  {storage?.ramUsedKb ?? 128} / {storage?.ramTotalKb ?? 320} KB
                </span>
              </div>
              <Progress value={ramPct} className="h-1.5" />
            </div>
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground">Logs</span>
                <span className="tabular-nums">
                  {storage?.logsUsedKb ?? 82} / {storage?.logsTotalKb ?? 256} KB
                </span>
              </div>
              <Progress value={logsPct} className="h-1.5" />
            </div>
          </div>
        </div>
      </div>

      <div className="grid grid-cols-2 md:grid-cols-4 gap-2 sm:gap-3 mt-3">
        <StatCard
          label="MikroTik"
          value={status?.mikrotik.ok ? "Connected" : "Disconnected"}
          icon={Router}
          tone={status?.mikrotik.ok ? "success" : "danger"}
          hint={status?.mikrotik.host ?? "10.0.0.1"}
        />
        <StatCard
          label="Internet"
          value={status?.internet.ok ? "Connected" : "Disconnected"}
          icon={Globe}
          tone={status?.internet.ok ? "success" : "danger"}
          hint={`${status?.internet.latencyMs ?? 32} ms`}
        />
        <StatCard
          label="Coin Slot"
          value={status?.coinSlot.state ?? "Idle"}
          icon={Coins}
          hint={`${status?.coinSlot.pulsesToday ?? 0} pulses`}
        />
        <StatCard
          label="Hotspot"
          value={status?.hotspot.ok ? "Online" : "Offline"}
          icon={Wifi}
          tone={status?.hotspot.ok ? "success" : "danger"}
          hint={`SSID: ${status?.hotspot.ssid ?? "Renz-Fi"}`}
        />
      </div>
    </div>
  );
}
