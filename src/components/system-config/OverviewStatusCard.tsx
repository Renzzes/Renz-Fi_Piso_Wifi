import { Skeleton } from "@/components/ui/skeleton";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import type { StatusTone } from "@/lib/dashboardDisplay";

export function OverviewStatusCard({
  title,
  statusLabel,
  statusTone,
  detail,
  loading = false,
}: {
  title: string;
  statusLabel: string;
  statusTone: StatusTone;
  detail: string;
  loading?: boolean;
}) {
  return (
    <div className="min-w-0 rounded-[14px] border bg-card p-4">
      <div className="text-[11px] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
        {title}
      </div>
      {loading ? (
        <Skeleton className="mt-2 h-5 w-24" />
      ) : (
        <div className="mt-2">
          <ConfigStatusBadge label={statusLabel} tone={statusTone} />
        </div>
      )}
      <div className="mt-2 truncate font-mono text-[13px]">
        {loading ? <Skeleton className="h-4 w-28" /> : detail}
      </div>
    </div>
  );
}
