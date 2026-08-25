import { cn } from "@/lib/utils";
import type { StatusTone } from "@/lib/dashboardDisplay";

export function ConfigStatusBadge({ label, tone }: { label: string; tone: StatusTone }) {
  const styles: Record<StatusTone, string> = {
    ok: "bg-emerald-500/15 text-emerald-700 dark:text-emerald-400",
    warn: "bg-amber-500/15 text-amber-800 dark:text-amber-300",
    bad: "bg-red-500/15 text-red-700 dark:text-red-400",
    unknown: "bg-slate-500/15 text-slate-600 dark:text-slate-300",
    neutral: "bg-slate-500/15 text-slate-600 dark:text-slate-400",
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
        "inline-flex items-center gap-1.5 rounded-full px-2 py-0.5 text-[11px] font-medium",
        styles[tone],
      )}
    >
      <span className={cn("h-1.5 w-1.5 rounded-full", dots[tone])} aria-hidden />
      {label}
    </span>
  );
}
