import {
  CardError,
  CardSkeleton,
  DashboardCard,
  Sparkline,
  StatusBadge,
} from "@/components/dashboard/DashboardPrimitives";
import { cn } from "@/lib/utils";
import type { StatusTone } from "@/lib/dashboardDisplay";

export type NetworkStatusRow = {
  label: string;
  value: string;
  tone: StatusTone;
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
            : tone === "warn"
              ? "bg-white text-amber-800 dark:bg-white/95 dark:text-amber-800"
              : "bg-white/90 text-muted-foreground dark:bg-white/90",
      )}
    >
      <span
        className={cn(
          "h-2 w-2 rounded-full",
          positive
            ? "bg-emerald-500"
            : tone === "bad"
              ? "bg-red-500"
              : tone === "warn"
                ? "bg-amber-500"
                : "bg-slate-400",
        )}
        aria-hidden
      />
      {label}
    </span>
  );
}

function SessionActivityChart({
  loading,
  history,
  current,
}: {
  loading: boolean;
  history: number[];
  current: number | undefined;
}) {
  const hasHistory = history.length >= 2;
  const latest = current ?? (history.length ? history[history.length - 1] : undefined);

  return (
    <div className="mt-5 rounded-xl border border-border/60 bg-muted/20 px-4 py-3">
      <div className="mb-2 flex min-w-0 items-end justify-between gap-3">
        <div className="min-w-0">
          <p className="text-[11px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">
            Active hotspot sessions
          </p>
          <p className="text-[12px] text-muted-foreground">
            Rolling history from Core <code className="text-[11px]">/api/status</code>
          </p>
        </div>
        <p className="shrink-0 text-2xl font-semibold tabular-nums text-foreground">
          {loading && latest === undefined ? "…" : (latest ?? "—")}
        </p>
      </div>
      {hasHistory ? (
        <div className="h-16 w-full min-w-0">
          <Sparkline values={history} />
        </div>
      ) : (
        <p className="py-4 text-center text-[12px] text-muted-foreground">
          {loading
            ? "Waiting for session telemetry…"
            : "Session count will appear here as Core status refreshes."}
        </p>
      )}
    </div>
  );
}

export function NetworkStatusCard({
  loading,
  error,
  onRetry,
  rows,
  stale,
  sessionHistory,
  sessionCount,
}: {
  loading: boolean;
  error: boolean;
  onRetry: () => void;
  rows: NetworkStatusRow[];
  stale?: boolean;
  sessionHistory?: number[];
  sessionCount?: number;
}) {
  if (loading && rows.length === 0) return <CardSkeleton rows={4} />;
  if (error && rows.length === 0) {
    return (
      <CardError
        title="Network Status"
        message="Unable to retrieve network status."
        onRetry={onRetry}
      />
    );
  }

  return (
    <DashboardCard className="flex h-full min-w-0 flex-col rounded-2xl border-border/80 p-5 shadow-sm sm:p-6">
      <div className="mb-5 flex min-w-0 items-start justify-between gap-3">
        <h3 className="text-xl font-semibold tracking-tight text-foreground">Network Status</h3>
        {stale ? <StatusBadge label="Stale data" tone="warn" /> : null}
      </div>

      <div className="divide-y divide-border/60 border-y border-border/60">
        {rows.map((row) => (
          <div
            key={row.label}
            className="flex min-w-0 items-center justify-between gap-4 py-3.5 first:pt-0 last:pb-0 sm:py-4"
          >
            <span className="truncate text-[15px] font-medium text-foreground">{row.label}</span>
            <ReferenceStatusPill label={row.value} tone={row.tone} />
          </div>
        ))}
      </div>

      <SessionActivityChart
        loading={loading}
        history={sessionHistory ?? []}
        current={sessionCount}
      />
    </DashboardCard>
  );
}
