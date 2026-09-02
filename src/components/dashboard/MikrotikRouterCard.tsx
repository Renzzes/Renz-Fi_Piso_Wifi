import { Router } from "lucide-react";
import {
  CardError,
  CardSkeleton,
  DashboardCard,
  MetricRing,
  StatusBadge,
} from "@/components/dashboard/DashboardPrimitives";
import { cn } from "@/lib/utils";
import type { StatusTone } from "@/lib/dashboardDisplay";

export type MikrotikRouterSnapshot = {
  model: string;
  routerOs: string;
  uptime: string;
  connection: string;
  connectionTone: StatusTone;
  cpu: string;
  memory: string;
  storage: {
    total: string;
    used: string;
    available: string;
    usagePctLabel: string;
    usagePctValue: number;
    hasData: boolean;
  };
  temperature: string;
  lastSyncLabel?: string;
};

function ReferenceStatusPill({ label, tone }: { label: string; tone: StatusTone }) {
  const positive = tone === "ok";
  return (
    <span
      className={cn(
        "inline-flex shrink-0 items-center gap-2 rounded-full px-3 py-1 text-[13px] font-medium shadow-sm",
        positive
          ? "bg-white text-emerald-700 dark:bg-white/95 dark:text-emerald-700"
          : tone === "bad"
            ? "bg-white text-red-700 dark:bg-white/95 dark:text-red-700"
            : "bg-white/90 text-muted-foreground dark:bg-white/90",
      )}
    >
      <span
        className={cn(
          "h-2 w-2 rounded-full",
          positive ? "bg-emerald-500" : tone === "bad" ? "bg-red-500" : "bg-slate-400",
        )}
        aria-hidden
      />
      {label}
    </span>
  );
}

function StatCell({
  label,
  value,
  valueClassName,
}: {
  label: string;
  value: string;
  valueClassName?: string;
}) {
  return (
    <div className="min-w-0">
      <div className="text-[12px] text-muted-foreground">{label}</div>
      <div
        className={cn(
          "mt-1 truncate text-[15px] font-semibold tabular-nums text-foreground sm:text-[16px]",
          valueClassName,
        )}
      >
        {value}
      </div>
    </div>
  );
}

function StorageBreakdownRow({
  label,
  value,
  percent,
  dotClassName,
}: {
  label: string;
  value: string;
  percent: string;
  dotClassName: string;
}) {
  return (
    <div className="flex min-w-0 items-center justify-between gap-3 border-b border-border/40 py-2.5 last:border-b-0">
      <div className="flex min-w-0 items-center gap-2.5">
        <span className={cn("h-2.5 w-2.5 shrink-0 rounded-full", dotClassName)} aria-hidden />
        <span className="truncate text-[14px] text-foreground">{label}</span>
      </div>
      <div className="shrink-0 text-right">
        <div className="text-[14px] font-semibold tabular-nums text-foreground">{value}</div>
        <div className="text-[12px] tabular-nums text-muted-foreground">{percent}</div>
      </div>
    </div>
  );
}

export function MikrotikRouterCard({
  loading,
  error,
  onRetry,
  snapshot,
  stale,
  showStorageSyncHint = false,
}: {
  loading: boolean;
  error: boolean;
  onRetry: () => void;
  snapshot: MikrotikRouterSnapshot;
  stale?: boolean;
  showStorageSyncHint?: boolean;
}) {
  if (loading) return <CardSkeleton rows={8} />;
  if (error && !snapshot.storage.hasData && snapshot.model === "N/A") {
    return (
      <CardError
        title="MikroTik Router"
        message="Unable to retrieve MikroTik router status."
        onRetry={onRetry}
      />
    );
  }

  const { storage } = snapshot;
  const availablePct =
    storage.hasData && storage.usagePctValue >= 0
      ? `${Math.max(0, 100 - storage.usagePctValue)}%`
      : "N/A";

  return (
    <DashboardCard className="flex h-full min-h-[32rem] min-w-0 flex-col rounded-2xl border-border/80 p-5 shadow-sm sm:p-6">
      <div className="mb-5 flex min-w-0 items-start justify-between gap-3">
        <div className="flex min-w-0 items-center gap-3">
          <span className="flex h-11 w-11 shrink-0 items-center justify-center rounded-xl bg-gradient-to-br from-emerald-500 to-teal-600 text-white shadow-sm">
            <Router className="h-5 w-5" aria-hidden />
          </span>
          <div className="min-w-0">
            <h3 className="truncate text-xl font-semibold tracking-tight text-foreground">
              MikroTik Router
            </h3>
            {snapshot.lastSyncLabel ? (
              <p className="mt-0.5 truncate text-[11px] text-muted-foreground/80">
                Last sync: {snapshot.lastSyncLabel}
              </p>
            ) : null}
          </div>
        </div>
        <div className="flex shrink-0 flex-col items-end gap-1.5">
          {stale ? <StatusBadge label="Stale cache" tone="warn" /> : null}
          <ReferenceStatusPill label={snapshot.connection} tone={snapshot.connectionTone} />
        </div>
      </div>

      <div className="grid grid-cols-1 gap-4 border-b border-border/60 pb-5 sm:grid-cols-3 sm:gap-5">
        <StatCell label="Model" value={snapshot.model} />
        <StatCell label="RouterOS" value={snapshot.routerOs} />
        <StatCell label="Uptime" value={snapshot.uptime} />
      </div>

      <div className="mt-5 grid grid-cols-1 gap-4 sm:grid-cols-3 sm:gap-5">
        <StatCell
          label="Connection"
          value={snapshot.connection}
          valueClassName={
            snapshot.connectionTone === "ok" ? "text-emerald-600 dark:text-emerald-400" : undefined
          }
        />
        <StatCell label="CPU" value={snapshot.cpu} />
        <StatCell label="Memory" value={snapshot.memory} />
      </div>

      <div className="mt-6 border-t border-border/60 pt-5">
        <h4 className="mb-4 text-[15px] font-semibold text-emerald-600 dark:text-emerald-400">
          Storage Overview
        </h4>

        {storage.hasData ? (
          <div className="grid grid-cols-1 gap-5 lg:grid-cols-[minmax(140px,180px)_1fr] lg:items-center">
            <MetricRing
              valueLabel={storage.hasData ? `${storage.usagePctValue.toFixed(1)}%` : "N/A"}
              caption="USED"
              percent={storage.usagePctValue}
              color="#10B981"
            />
            <div className="min-w-0">
              <StorageBreakdownRow
                label="Used storage"
                value={storage.used}
                percent={storage.usagePctLabel}
                dotClassName="bg-emerald-500"
              />
              <StorageBreakdownRow
                label="Available storage"
                value={storage.available}
                percent={availablePct}
                dotClassName="bg-slate-400"
              />
            </div>
          </div>
        ) : (
          <div className="rounded-xl border border-dashed border-border/70 bg-muted/15 px-4 py-6 text-center">
            <p className="text-[13px] text-muted-foreground">
              Storage data unavailable. Use Synchronize Router on the dashboard toolbar.
            </p>
          </div>
        )}

        {showStorageSyncHint ? (
          <p className="mt-3 text-[11px] leading-snug text-muted-foreground/90">
            RouterOS storage appears after the next successful router synchronization.
          </p>
        ) : null}
      </div>

      <div className="mt-auto grid grid-cols-2 gap-4 border-t border-border/60 pt-5 sm:grid-cols-4 sm:gap-3">
        <StatCell
          label="Total Storage"
          value={storage.total}
          valueClassName="text-emerald-600 dark:text-emerald-400"
        />
        <StatCell
          label="Used Storage"
          value={storage.used}
          valueClassName="text-emerald-600 dark:text-emerald-400"
        />
        <StatCell
          label="Available Storage"
          value={storage.available}
          valueClassName="text-emerald-600 dark:text-emerald-400"
        />
        <StatCell
          label="Temperature"
          value={snapshot.temperature}
          valueClassName="text-emerald-600 dark:text-emerald-400"
        />
      </div>
    </DashboardCard>
  );
}
