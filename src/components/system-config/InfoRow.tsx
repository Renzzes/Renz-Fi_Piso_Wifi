import type { ReactNode } from "react";
import { cn } from "@/lib/utils";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import type { StatusTone } from "@/lib/dashboardDisplay";

export function InfoRow({
  label,
  value,
  mono = false,
  tone,
}: {
  label: string;
  value: ReactNode;
  mono?: boolean;
  tone?: StatusTone;
}) {
  return (
    <div className="flex items-center justify-between gap-3 border-b border-border/60 py-2 text-[13px] last:border-0">
      <span className="min-w-0 text-muted-foreground">{label}</span>
      {tone ? (
        <ConfigStatusBadge label={String(value)} tone={tone} />
      ) : (
        <span
          className={cn(
            "min-w-0 max-w-[58%] break-words text-right font-medium leading-snug",
            mono && "font-mono text-[12px] tabular-nums",
          )}
        >
          {value}
        </span>
      )}
    </div>
  );
}

export function MetricTile({
  label,
  value,
  tone,
}: {
  label: string;
  value: string;
  tone?: StatusTone;
}) {
  return (
    <div className="min-w-0 rounded-lg border bg-muted/20 px-3 py-2.5">
      <div className="text-[11px] font-medium text-muted-foreground">{label}</div>
      <div className="mt-1">
        {tone ? (
          <ConfigStatusBadge label={value} tone={tone} />
        ) : (
          <div className="truncate font-mono text-[13px] font-medium tabular-nums">{value}</div>
        )}
      </div>
    </div>
  );
}
