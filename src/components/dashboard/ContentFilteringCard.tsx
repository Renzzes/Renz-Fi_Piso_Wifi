import { ShieldBan } from "lucide-react";
import {
  CardError,
  CardLink,
  CardSkeleton,
  DashboardCard,
  DashboardCardHeader,
  StatusBadge,
  StatusRow,
} from "@/components/dashboard/DashboardPrimitives";
import {
  contentFilterStatusLabel,
  recentBlockedDomains,
  type ContentFilterState,
} from "@/services/contentFilter";
import type { StatusTone } from "@/lib/dashboardDisplay";

function statusTone(state: ContentFilterState | undefined): StatusTone {
  if (!state) return "unknown";
  if (state.lastSyncError) return "warn";
  return state.enabled ? "ok" : "neutral";
}

export function ContentFilteringCard({
  loading,
  error,
  onRetry,
  state,
}: {
  loading: boolean;
  error: boolean;
  onRetry: () => void;
  state: ContentFilterState | undefined;
}) {
  if (loading && !state) return <CardSkeleton rows={5} />;
  if (error && !state) {
    return (
      <CardError
        title="Content Filtering"
        message="Unable to retrieve content filtering status."
        onRetry={onRetry}
      />
    );
  }

  const domains = state?.domains ?? [];
  const blockedCount = domains.length;
  const recent = recentBlockedDomains(domains);
  const statusLabel = contentFilterStatusLabel(state);
  const tone = statusTone(state);

  return (
    <DashboardCard>
      <DashboardCardHeader
        title="Content Filtering"
        action={<StatusBadge label={statusLabel} tone={tone} />}
      />
      <div className="mb-3 flex items-center gap-2 text-muted-foreground">
        <ShieldBan className="h-4 w-4 shrink-0 text-primary" aria-hidden />
        <p className="text-[12px]">Guest-network domain blocking via MikroTik</p>
      </div>
      <div>
        <StatusRow label="Status" value={statusLabel} tone={tone} />
        <StatusRow
          label="Blocked Websites"
          value={loading ? "Loading..." : String(blockedCount)}
          tone="neutral"
        />
      </div>
      <div className="mt-3 border-t pt-3">
        <p className="mb-2 text-[11px] font-medium uppercase tracking-wide text-muted-foreground">
          Recently blocked
        </p>
        {recent.length > 0 ? (
          <ul className="space-y-1 text-[12px] text-foreground">
            {recent.map((domain) => (
              <li key={domain} className="truncate font-mono">
                • {domain}
              </li>
            ))}
          </ul>
        ) : (
          <p className="text-[12px] text-muted-foreground">No blocking activity recorded</p>
        )}
      </div>
      <div className="mt-3 flex justify-end">
        <CardLink to="/content-filtering">Manage Filtering</CardLink>
      </div>
    </DashboardCard>
  );
}
