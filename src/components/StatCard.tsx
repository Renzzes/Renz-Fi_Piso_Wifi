import type { LucideIcon } from "lucide-react";
import { cn } from "@/lib/utils";

export function StatCard({
  label,
  value,
  hint,
  icon: Icon,
  tone = "default",
}: {
  label: string;
  value: string | number;
  hint?: string;
  icon?: LucideIcon;
  tone?: "default" | "success" | "warning" | "danger";
}) {
  const toneClass =
    tone === "success"
      ? "text-emerald-600 dark:text-emerald-400"
      : tone === "warning"
        ? "text-amber-600 dark:text-amber-400"
        : tone === "danger"
          ? "text-red-600 dark:text-red-400"
          : "text-foreground";
  return (
    <div className="rounded-md border bg-card p-3 flex flex-col gap-1">
      <div className="flex items-center justify-between text-xs text-muted-foreground">
        <span>{label}</span>
        {Icon && <Icon className="h-3.5 w-3.5" />}
      </div>
      <div className={cn("text-xl font-semibold tabular-nums", toneClass)}>{value}</div>
      {hint && <div className="text-[11px] text-muted-foreground">{hint}</div>}
    </div>
  );
}

export function StatusRow({
  label,
  status,
  ok,
}: {
  label: string;
  status: string;
  ok: boolean;
}) {
  return (
    <div className="flex items-center justify-between py-2 border-b last:border-0 text-sm">
      <span className="text-muted-foreground">{label}</span>
      <span className="flex items-center gap-2">
        <span
          className={cn(
            "h-2 w-2 rounded-full",
            ok ? "bg-emerald-500" : "bg-red-500",
          )}
        />
        <span className="font-medium">{status}</span>
      </span>
    </div>
  );
}
