import type { ReactNode } from "react";
import { Link } from "react-router-dom";
import { cn } from "@/lib/utils";
import { Skeleton } from "@/components/ui/skeleton";
import type { StatusTone } from "@/lib/dashboardDisplay";

export function DashboardCard({
  className,
  children,
}: {
  className?: string;
  children: ReactNode;
}) {
  return (
    <section
      className={cn(
        "rounded-[14px] border bg-card p-4 text-card-foreground shadow-sm",
        className,
      )}
    >
      {children}
    </section>
  );
}

export function DashboardCardHeader({ title, action }: { title: string; action?: ReactNode }) {
  return (
    <div className="mb-3 flex min-w-0 items-center justify-between gap-2">
      <h3 className="min-w-0 truncate text-[13px] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
        {title}
      </h3>
      {action}
    </div>
  );
}

export function StatusBadge({
  label,
  tone,
  pulse = false,
}: {
  label: string;
  tone: StatusTone;
  pulse?: boolean;
}) {
  const styles: Record<StatusTone, string> = {
    ok: "bg-emerald-500/15 text-emerald-700 dark:text-emerald-400",
    warn: "bg-amber-500/15 text-amber-800 dark:text-amber-300",
    bad: "bg-red-500/15 text-red-700 dark:text-red-400",
    unknown: "bg-muted text-muted-foreground",
    neutral: "bg-muted text-muted-foreground",
  };
  const dots: Record<StatusTone, string> = {
    ok: "bg-emerald-500",
    warn: "bg-amber-500",
    bad: "bg-red-500",
    unknown: "bg-slate-400",
    neutral: "bg-slate-500",
  };
  return (
    <span
      className={cn(
        "inline-flex max-w-full items-center gap-1.5 truncate rounded-full px-2 py-0.5 text-[11px] font-medium",
        styles[tone],
      )}
    >
      <span
        className={cn("h-1.5 w-1.5 shrink-0 rounded-full", dots[tone], pulse && "animate-pulse")}
        aria-hidden
      />
      <span className="truncate">{label}</span>
    </span>
  );
}

export function StatusDot({ tone }: { tone: StatusTone }) {
  const dots: Record<StatusTone, string> = {
    ok: "bg-emerald-500",
    warn: "bg-amber-500",
    bad: "bg-red-500",
    unknown: "bg-slate-400",
    neutral: "bg-slate-500",
  };
  return <span className={cn("h-2 w-2 shrink-0 rounded-full", dots[tone])} aria-hidden />;
}

export function StatusRow({
  label,
  value,
  tone,
  mono = false,
}: {
  label: string;
  value: string;
  tone: StatusTone;
  mono?: boolean;
}) {
  const valueClass: Record<StatusTone, string> = {
    ok: "text-foreground",
    warn: "text-amber-800 dark:text-amber-300",
    bad: "text-red-700 dark:text-red-400",
    unknown: "text-muted-foreground",
    neutral: "text-muted-foreground",
  };
  return (
    <div className="flex items-center justify-between gap-3 py-1.5 text-[13px]">
      <span className="flex min-w-0 items-center gap-2 text-muted-foreground">
        <StatusDot tone={tone} />
        <span className="truncate">{label}</span>
      </span>
      <span
        className={cn(
          "min-w-0 max-w-[55%] truncate text-right font-medium",
          mono && "font-mono text-[12px]",
          valueClass[tone],
        )}
      >
        {value}
      </span>
    </div>
  );
}

export function CardLink({ to, children }: { to: string; children: ReactNode }) {
  return (
    <Link
      to={to}
      className="rounded-sm text-[12px] font-medium text-primary hover:text-primary/80 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
    >
      {children}
    </Link>
  );
}

export function MetricRing({
  valueLabel,
  caption,
  percent,
  color,
}: {
  valueLabel: string;
  caption: string;
  percent: number;
  color: string;
}) {
  const size = 148;
  const stroke = 10;
  const r = (size - stroke) / 2;
  const c = 2 * Math.PI * r;
  const clamped = Math.min(100, Math.max(0, percent));
  const dash = (clamped / 100) * c;
  return (
    <div className="relative mx-auto h-[min(148px,40vw)] w-[min(148px,40vw)] max-h-[148px] max-w-[148px]">
      <svg viewBox={`0 0 ${size} ${size}`} className="h-full w-full -rotate-90" aria-hidden>
        <circle
          cx={size / 2}
          cy={size / 2}
          r={r}
          fill="none"
          stroke="var(--border)"
          strokeWidth={stroke}
        />
        <circle
          cx={size / 2}
          cy={size / 2}
          r={r}
          fill="none"
          stroke={color}
          strokeWidth={stroke}
          strokeLinecap="round"
          strokeDasharray={`${dash} ${c - dash}`}
          className="transition-[stroke-dasharray] duration-700"
        />
      </svg>
      <div className="absolute inset-0 flex flex-col items-center justify-center text-center">
        <div className="text-[28px] font-semibold tabular-nums leading-none text-foreground">
          {valueLabel}
        </div>
        <div className="mt-1 text-[11px] text-muted-foreground">{caption}</div>
      </div>
    </div>
  );
}

export function HealthBar({
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
    <div className="space-y-1.5">
      <div className="flex min-w-0 items-center justify-between gap-2 text-[12px]">
        <span className="min-w-0 truncate text-muted-foreground">{label}</span>
        <span className="shrink-0 font-mono tabular-nums text-foreground">{value}</span>
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

export function Sparkline({ values }: { values: number[] }) {
  if (values.length < 2) return null;
  const max = Math.max(...values, 1);
  const min = Math.min(...values, 0);
  const range = max - min || 1;
  const w = 140;
  const h = 36;
  const pts = values
    .map((v, i) => {
      const x = (i / (values.length - 1)) * w;
      const y = h - ((v - min) / range) * (h - 6) - 3;
      return `${x},${y}`;
    })
    .join(" ");
  return (
    <svg viewBox={`0 0 ${w} ${h}`} className="h-9 w-full" aria-hidden>
      <polyline fill="none" stroke="#F59E0B" strokeWidth="2" strokeLinejoin="round" points={pts} />
    </svg>
  );
}

export function CardSkeleton({ rows = 4 }: { rows?: number }) {
  return (
    <DashboardCard>
      <Skeleton className="mb-3 h-4 w-28" />
      <div className="space-y-2">
        {Array.from({ length: rows }).map((_, i) => (
          <Skeleton key={i} className="h-3 w-full" />
        ))}
      </div>
    </DashboardCard>
  );
}

export function CardError({
  title,
  message,
  onRetry,
}: {
  title: string;
  message: string;
  onRetry?: () => void;
}) {
  return (
    <DashboardCard>
      <DashboardCardHeader title={title} />
      <p className="text-[13px] text-muted-foreground">{message}</p>
      {onRetry ? (
        <button
          type="button"
          onClick={onRetry}
          className="mt-3 rounded-sm text-[12px] font-medium text-primary hover:text-primary/80 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          Retry
        </button>
      ) : null}
    </DashboardCard>
  );
}

export function RingStat({ value, label }: { value: string; label: string }) {
  return (
    <div className="min-w-0 text-center">
      <div className="truncate text-[15px] font-semibold tabular-nums text-foreground">{value}</div>
      <div className="text-[11px] text-muted-foreground">{label}</div>
    </div>
  );
}
