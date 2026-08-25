import type { LucideIcon } from "lucide-react";
import { Coins, Globe, Monitor, Router, Wifi } from "lucide-react";
import { DashboardCard, StatusBadge } from "@/components/dashboard/DashboardPrimitives";
import type { StatusTone } from "@/lib/dashboardDisplay";

function ConnectivityCard({
  title,
  value,
  hint,
  tone,
  icon: Icon,
}: {
  title: string;
  value: string;
  hint: string;
  tone: StatusTone;
  icon: LucideIcon;
}) {
  return (
    <DashboardCard className="min-w-0 p-3">
      <div className="mb-2 flex items-center justify-between gap-2">
        <span className="min-w-0 truncate text-[12px] font-medium text-foreground">{title}</span>
        <Icon className="h-3.5 w-3.5 shrink-0 text-muted-foreground" aria-hidden />
      </div>
      <StatusBadge label={value} tone={tone} pulse={tone === "ok"} />
      <p className="mt-2 text-[11px] leading-snug text-muted-foreground">{hint}</p>
    </DashboardCard>
  );
}

export function ConnectivityStatusRow({
  mikrotikLabel,
  mikrotikTone,
  mikrotikHint,
  adminLabel,
  adminTone,
  wanLabel,
  wanTone,
  wanHint,
  showWan,
  coinLabel,
  coinTone,
  coinHint,
  hotspotLabel,
  hotspotTone,
}: {
  mikrotikLabel: string;
  mikrotikTone: StatusTone;
  mikrotikHint: string;
  adminLabel: string;
  adminTone: StatusTone;
  wanLabel: string;
  wanTone: StatusTone;
  wanHint: string;
  showWan: boolean;
  coinLabel: string;
  coinTone: StatusTone;
  coinHint: string;
  hotspotLabel: string;
  hotspotTone: StatusTone;
}) {
  return (
    <div className="grid grid-cols-1 gap-2 sm:grid-cols-2 xl:grid-cols-5">
      <ConnectivityCard
        title="MikroTik Router"
        value={mikrotikLabel}
        hint={mikrotikHint}
        tone={mikrotikTone}
        icon={Router}
      />
      <ConnectivityCard
        title="Admin Connection"
        value={adminLabel}
        hint="Browser • ESP32 API"
        tone={adminTone}
        icon={Monitor}
      />
      {showWan ? (
        <ConnectivityCard
          title="WAN Internet"
          value={wanLabel}
          hint={wanHint}
          tone={wanTone}
          icon={Globe}
        />
      ) : (
        <ConnectivityCard
          title="WAN Internet"
          value="Unknown"
          hint="N/A"
          tone="unknown"
          icon={Globe}
        />
      )}
      <ConnectivityCard
        title="Coin Slot"
        value={coinLabel}
        hint={coinHint}
        tone={coinTone}
        icon={Coins}
      />
      <ConnectivityCard
        title="Hotspot"
        value={hotspotLabel}
        hint="MikroTik Hotspot"
        tone={hotspotTone}
        icon={Wifi}
      />
    </div>
  );
}
