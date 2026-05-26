import { createFileRoute } from "@tanstack/react-router";
import {
  Activity,
  DollarSign,
  HardDrive,
  Users,
  Wifi,
  Coins,
  Router,
  Globe,
} from "lucide-react";
import { StatCard, StatusRow } from "@/components/StatCard";
import { PageHeader } from "@/components/PageHeader";
import { Progress } from "@/components/ui/progress";

export const Route = createFileRoute("/_layout/dashboard")({
  component: DashboardPage,
});

function DashboardPage() {
  return (
    <div>
      <PageHeader title="Dashboard" description="Overview of your Renz-Fi system" />

      <div className="grid grid-cols-2 md:grid-cols-4 gap-2 sm:gap-3">
        <StatCard label="Today's Sales" value="₱ 248" icon={DollarSign} tone="success" hint="32 sessions" />
        <StatCard label="Weekly Sales" value="₱ 1,820" icon={DollarSign} hint="208 sessions" />
        <StatCard label="Monthly Sales" value="₱ 7,415" icon={DollarSign} hint="842 sessions" />
        <StatCard label="Active Users" value={12} icon={Users} tone="success" hint="2 idle" />
      </div>

      <div className="grid md:grid-cols-2 gap-3 mt-3">
        <div className="rounded-md border bg-card p-3">
          <div className="text-sm font-medium mb-2 flex items-center gap-2">
            <Activity className="h-4 w-4" /> System Status
          </div>
          <StatusRow label="MikroTik Router" status="Connected" ok />
          <StatusRow label="Internet" status="Online" ok />
          <StatusRow label="Coin Slot" status="Ready" ok />
          <StatusRow label="ESP32 Uptime" status="3d 4h 12m" ok />
        </div>

        <div className="rounded-md border bg-card p-3">
          <div className="text-sm font-medium mb-2 flex items-center gap-2">
            <HardDrive className="h-4 w-4" /> Storage Usage
          </div>
          <div className="space-y-3 text-xs">
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground">Flash (SPIFFS)</span>
                <span className="tabular-nums">1.2 / 3.0 MB</span>
              </div>
              <Progress value={40} className="h-1.5" />
            </div>
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground">RAM</span>
                <span className="tabular-nums">128 / 320 KB</span>
              </div>
              <Progress value={40} className="h-1.5" />
            </div>
            <div>
              <div className="flex justify-between mb-1">
                <span className="text-muted-foreground">Logs</span>
                <span className="tabular-nums">82 / 256 KB</span>
              </div>
              <Progress value={32} className="h-1.5" />
            </div>
          </div>
        </div>
      </div>

      <div className="grid grid-cols-2 md:grid-cols-4 gap-2 sm:gap-3 mt-3">
        <StatCard label="MikroTik" value="OK" icon={Router} tone="success" hint="10.0.0.1" />
        <StatCard label="Internet" value="OK" icon={Globe} tone="success" hint="32 ms" />
        <StatCard label="Coin Slot" value="Idle" icon={Coins} hint="0 pulses" />
        <StatCard label="Hotspot" value="Up" icon={Wifi} tone="success" hint="SSID: Renz-Fi" />
      </div>
    </div>
  );
}
