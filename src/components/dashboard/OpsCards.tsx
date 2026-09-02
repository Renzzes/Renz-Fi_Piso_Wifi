import type { LucideIcon } from "lucide-react";
import { Link } from "react-router-dom";
import { BarChart3, Globe, Settings, Tag, Ticket, Wifi } from "lucide-react";
import {
  CardError,
  CardLink,
  CardSkeleton,
  DashboardCard,
  DashboardCardHeader,
  HealthBar,
  StatusBadge,
} from "@/components/dashboard/DashboardPrimitives";
import { formatMb, formatStorageFromMb } from "@/lib/dashboardFormat";
import { healthLevelLabel, type StatusTone } from "@/lib/dashboardDisplay";
import { cn } from "@/lib/utils";
import { pathPermission, type OperatorPermission } from "@/lib/operatorPermissions";

export function SystemHealthCard({
  loading,
  error,
  onRetry,
  level,
  cpuMhz,
  cpuPct,
  tempC,
  tempPct,
  tempAvailable,
  heapLabel,
  heapPct,
  psramLabel,
  psramPct,
  ethernetDriver,
  ethernetLink,
}: {
  loading: boolean;
  error: boolean;
  onRetry: () => void;
  level: string | undefined;
  cpuMhz: number | undefined;
  cpuPct: number;
  tempC: number | undefined;
  tempPct: number;
  tempAvailable: boolean;
  heapLabel: string;
  heapPct: number;
  psramLabel: string;
  psramPct: number;
  ethernetDriver: string;
  ethernetLink: string;
}) {
  if (loading && level === undefined) return <CardSkeleton rows={6} />;
  if (error && level === undefined) {
    return (
      <CardError
        title="System Health"
        message="Unable to retrieve system health."
        onRetry={onRetry}
      />
    );
  }
  const tone: StatusTone =
    level === "HEALTHY" || level === "ACTIVE_SESSION"
      ? "ok"
      : level === "WARNING"
        ? "warn"
        : level === "ERROR"
          ? "bad"
          : "unknown";
  return (
    <DashboardCard>
      <DashboardCardHeader
        title="System Health"
        action={<StatusBadge label={healthLevelLabel(level)} tone={tone} />}
      />
      <div className="space-y-3">
        <HealthBar
          label="ESP32 CPU"
          value={cpuMhz == null ? "N/A" : `${cpuMhz} MHz`}
          percent={cpuPct}
          colorClass="bg-[#2FE0C4]"
        />
        <HealthBar
          label="Chip Temperature"
          value={tempAvailable && tempC != null ? `${tempC.toFixed(1)} °C` : "N/A"}
          percent={tempAvailable ? tempPct : 0}
          colorClass="bg-[#F59E0B]"
        />
        <HealthBar
          label="Heap Free"
          value={heapLabel}
          percent={heapPct}
          colorClass="bg-[#8B7CF6]"
        />
        <HealthBar
          label="PSRAM Free"
          value={psramLabel}
          percent={psramPct}
          colorClass="bg-[#8B7CF6]"
        />
      </div>
      <div className="mt-3 space-y-1 border-t pt-3 text-[11px] text-muted-foreground">
        <div className="flex justify-between gap-2">
          <span>Ethernet driver</span>
          <span className="min-w-0 truncate font-mono text-foreground">{ethernetDriver}</span>
        </div>
        <div className="flex justify-between gap-2">
          <span>Ethernet link</span>
          <span className="min-w-0 truncate font-mono text-foreground">{ethernetLink}</span>
        </div>
      </div>
    </DashboardCard>
  );
}

export function StorageUsageCard({
  loading,
  error,
  onRetry,
  flashUsed,
  flashTotal,
  flashPct,
  logsUsed,
  logsTotal,
  logsPct,
  sdReady,
  sdUsed,
  sdTotal,
  sdFree,
  sdPct,
  sdStatus,
  heapUsed,
  heapTotal,
  psramPresent,
  psramUsed,
  psramTotal,
  dmaFree,
  dmaLargest,
}: {
  loading: boolean;
  error: boolean;
  onRetry: () => void;
  flashUsed: number | undefined;
  flashTotal: number | undefined;
  flashPct: number;
  logsUsed: number | undefined;
  logsTotal: number | undefined;
  logsPct: number;
  sdReady: boolean;
  sdUsed: number | undefined;
  sdTotal: number | undefined;
  sdFree: number | undefined;
  sdPct: number;
  sdStatus: string;
  heapUsed: number | undefined;
  heapTotal: number | undefined;
  psramPresent: boolean;
  psramUsed: number | undefined;
  psramTotal: number | undefined;
  dmaFree: number | undefined;
  dmaLargest: number | undefined;
}) {
  if (loading && flashUsed === undefined) return <CardSkeleton rows={6} />;
  if (error && flashUsed === undefined) {
    return (
      <CardError
        title="Storage Usage"
        message="Unable to retrieve storage usage."
        onRetry={onRetry}
      />
    );
  }
  return (
    <DashboardCard>
      <DashboardCardHeader
        title="Storage Usage"
        action={<CardLink to="/system-settings">Manage</CardLink>}
      />
      <div className="space-y-3 text-[12px]">
        <StorageBar
          label="Flash (SPIFFS)"
          value={
            flashUsed === undefined ? "N/A" : `${formatMb(flashUsed)} / ${formatMb(flashTotal)} MB`
          }
          percent={flashPct}
          colorClass="bg-[#2FE0C4]"
        />
        <StorageBar
          label="Logs"
          value={logsUsed === undefined ? "N/A" : `${logsUsed} / ${logsTotal} KB`}
          percent={logsPct}
          colorClass="bg-[#F59E0B]"
        />
        <StorageBar
          label="SD Card"
          value={
            sdReady
              ? `${formatStorageFromMb(sdUsed)} / ${formatStorageFromMb(sdTotal)}`
              : sdStatus || "N/A"
          }
          percent={sdReady ? sdPct : 0}
          colorClass="bg-[#F59E0B]"
        />
      </div>
      <p className="mt-3 text-[12px] text-muted-foreground">
        Status: {sdStatus || "N/A"}
        {sdReady ? ` • Free: ${formatStorageFromMb(sdFree)}` : ""}
      </p>
      <div className="mt-2 space-y-1 text-[11px] text-muted-foreground">
        {heapUsed !== undefined && heapTotal !== undefined ? (
          <div className="flex justify-between gap-2">
            <span>Internal heap</span>
            <span className="tabular-nums">
              {heapUsed} / {heapTotal} KB
            </span>
          </div>
        ) : null}
        {psramPresent && psramUsed !== undefined && psramTotal !== undefined ? (
          <div className="flex justify-between gap-2">
            <span>PSRAM</span>
            <span className="tabular-nums">
              {psramUsed} / {psramTotal} KB
            </span>
          </div>
        ) : null}
        {dmaFree !== undefined && dmaLargest !== undefined ? (
          <div className="flex justify-between gap-2">
            <span>DMA free / largest</span>
            <span className="tabular-nums">
              {dmaFree} / {dmaLargest} KB
            </span>
          </div>
        ) : null}
      </div>
    </DashboardCard>
  );
}

export function StorageBar({
  label,
  value,
  percent,
  colorClass,
}: {
  label: string;
  value: string;
  percent: number;
  colorClass: string;
}) {
  return (
    <div>
      <div className="mb-1 flex min-w-0 justify-between gap-2">
        <span className="min-w-0 truncate text-muted-foreground">{label}</span>
        <span className="shrink-0 tabular-nums text-foreground">{value}</span>
      </div>
      <div className="h-2 overflow-hidden rounded-full bg-muted">
        <div
          className={cn("h-full rounded-full transition-all duration-500", colorClass)}
          style={{ width: `${Math.min(100, Math.max(0, percent))}%` }}
        />
      </div>
    </div>
  );
}

const QUICK_ACTIONS: Array<{
  to: string;
  label: string;
  icon: LucideIcon;
  permission?: OperatorPermission;
  ownerOnly?: boolean;
}> = [
  { to: "/promo-rates", label: "Promo Rates", icon: Tag, permission: "promo-rates" },
  { to: "/vouchers", label: "Generate Voucher", icon: Ticket, permission: "vouchers" },
  { to: "/sales-reports", label: "Sales Reports", icon: BarChart3, permission: "sales-reports" },
  { to: "/captive-portal", label: "Captive Portal", icon: Globe, permission: "captive-portal" },
  { to: "/access-points", label: "Access Points", icon: Wifi, ownerOnly: true },
  { to: "/system-settings", label: "System Settings", icon: Settings, ownerOnly: true },
];

export function QuickActionsBar({
  isOwner,
  permissions,
}: {
  isOwner: boolean;
  permissions: OperatorPermission[];
}) {
  const actions = QUICK_ACTIONS.filter((item) => {
    if (item.ownerOnly) return isOwner;
    if (isOwner) return true;
    const key = item.permission ?? pathPermission(item.to);
    if (!key) return false;
    return permissions.includes(key);
  });
  return (
    <div className="grid grid-cols-2 gap-2 sm:grid-cols-3 lg:grid-cols-6">
      {actions.map((item) => {
        const Icon = item.icon;
        return (
          <Link
            key={item.to}
            to={item.to}
            className="flex min-h-[72px] flex-col items-center justify-center gap-1.5 rounded-lg border border-border/70 bg-card px-2 py-2.5 text-center text-[11px] font-semibold text-foreground shadow-sm transition-colors hover:border-primary/40 hover:bg-primary/5 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <Icon className="h-4 w-4 shrink-0 text-primary" aria-hidden />
            <span className="leading-tight">{item.label}</span>
          </Link>
        );
      })}
    </div>
  );
}

/** @deprecated Use QuickActionsBar — kept for backward compatibility during layout migration. */
export function QuickActionsCard({
  isOwner,
  permissions,
}: {
  isOwner: boolean;
  permissions: OperatorPermission[];
}) {
  return (
    <DashboardCard>
      <DashboardCardHeader title="Quick Actions" />
      <QuickActionsBar isOwner={isOwner} permissions={permissions} />
    </DashboardCard>
  );
}
