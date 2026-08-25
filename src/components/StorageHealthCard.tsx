import { useEffect, useRef } from "react";
import { AlertTriangle, ChevronDown, HardDrive } from "lucide-react";
import { toast } from "sonner";
import { Progress } from "@/components/ui/progress";
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from "@/components/ui/collapsible";
import { useStorageHealth } from "@/hooks/api/useStorageHealth";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import { MetricTile } from "@/components/system-config/InfoRow";
import { Skeleton } from "@/components/ui/skeleton";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import type { StorageHealth, StorageStatus } from "@/types/api";
import type { StatusTone } from "@/lib/dashboardDisplay";

const HEALTH_STYLE: Record<StorageHealth, string> = {
  HEALTHY:
    "border-emerald-300 bg-emerald-50 text-emerald-800 dark:border-emerald-800 dark:bg-emerald-950/30 dark:text-emerald-300",
  DEGRADED:
    "border-orange-400 bg-orange-50 text-orange-800 dark:border-orange-800 dark:bg-orange-950/30 dark:text-orange-300",
  WARNING:
    "border-yellow-400 bg-yellow-50 text-yellow-900 dark:border-yellow-700 dark:bg-yellow-950/30 dark:text-yellow-300",
  CRITICAL:
    "border-red-400 bg-red-50 text-red-800 dark:border-red-800 dark:bg-red-950/30 dark:text-red-300",
  READ_ONLY:
    "border-orange-400 bg-orange-50 text-orange-800 dark:border-orange-800 dark:bg-orange-950/30 dark:text-orange-300",
  UNKNOWN:
    "border-slate-300 bg-slate-50 text-slate-700 dark:border-slate-700 dark:bg-slate-900/40 dark:text-slate-300",
};

const HEALTH_COPY: Record<StorageHealth, { status: string; description: string; action: string }> =
  {
    HEALTHY: {
      status: "Healthy",
      description: "The SD card is working normally and durable records are protected.",
      action: "No action is needed.",
    },
    DEGRADED: {
      status: "Degraded Mode",
      description: "The appliance is still operating on emergency internal storage.",
      action: "Reinsert or replace the SD card soon.",
    },
    WARNING: {
      status: "Warning",
      description: "Emergency storage is nearing its safe limit.",
      action: "Restore the SD card as soon as possible.",
    },
    CRITICAL: {
      status: "Critical",
      description: "Emergency storage is almost full. New durable records may soon be rejected.",
      action: "Reinsert or replace the SD card immediately.",
    },
    READ_ONLY: {
      status: "Read Only",
      description: "The SD card can be read but cannot safely accept new records.",
      action: "Back up the card and replace it soon.",
    },
    UNKNOWN: {
      status: "Unknown",
      description: "The appliance has not confirmed the current storage condition.",
      action: "Refresh this page. If the status remains unknown, check the SD card.",
    },
  };

function formatBytes(value?: number): string {
  if (value === undefined || !Number.isFinite(value)) return "Unknown";
  if (value < 1024) return `${value} B`;
  if (value < 1024 ** 2) return `${(value / 1024).toFixed(1)} KB`;
  if (value < 1024 ** 3) return `${(value / 1024 ** 2).toFixed(1)} MB`;
  return `${(value / 1024 ** 3).toFixed(1)} GB`;
}

function formatAge(seconds?: number | null): string {
  if (seconds === undefined || seconds === null) return "Not recorded since startup";
  if (seconds < 60) return `${seconds}s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  return `${Math.floor(seconds / 3600)}h ago`;
}

function known(value?: boolean | null): string {
  if (value === true) return "Healthy";
  if (value === false) return "Attention required";
  return "Unknown";
}

function notifyHealthChange(health: StorageHealth) {
  if (health === "HEALTHY") toast.success("SD storage recovered");
  else if (health === "CRITICAL") toast.error("SD Card Health changed to critical");
  else if (health === "READ_ONLY") toast.error("SD Card Health changed to read only");
  else toast.warning(`SD Card Health changed to ${health.replace("_", " ").toLowerCase()}`);
}

function notifyTransitionExtras(previous: StorageStatus | null, next: StorageStatus) {
  const prevConflicts = previous?.pendingConflicts ?? 0;
  const nextConflicts = next.pendingConflicts ?? 0;
  if (prevConflicts === 0 && nextConflicts > 0) {
    toast.warning(
      "SD and internal storage copies differ — review required (no automatic merge). The SD card can still be working.",
    );
  }

  const prevReplay = previous?.pendingReplay ?? previous?.pendingHistory ?? 0;
  const nextReplay = next.pendingReplay ?? next.pendingHistory ?? 0;
  if (prevReplay > 0 && nextReplay === 0 && (next.health === "HEALTHY" || next.writable)) {
    toast.success("Storage replay completed");
  }
}

function healthTone(health: StorageHealth): StatusTone {
  if (health === "HEALTHY") return "ok";
  if (health === "WARNING") return "warn";
  if (health === "CRITICAL" || health === "READ_ONLY") return "bad";
  if (health === "DEGRADED") return "warn";
  return "unknown";
}

export function StorageHealthCard({
  className,
  variant = "full",
}: {
  className?: string;
  variant?: "full" | "console";
}) {
  const { data, isLoading, isError, refetch } = useStorageHealth();
  const health = data?.health ?? "UNKNOWN";
  const copy = { ...HEALTH_COPY[health] };
  if (health === "HEALTHY" && (data?.pendingConflicts ?? 0) > 0) {
    copy.description =
      "The SD card is mounted and writable. An unresolved SPIFFS/SD copy difference needs owner review (no automatic merge).";
    copy.action =
      "Review the conflict list below. The card does not need to be replaced for this warning.";
  }
  const previousHealth = useRef<StorageHealth | null>(null);
  const previousStatus = useRef<StorageStatus | null>(null);

  useEffect(() => {
    if (!data?.health) return;
    if (previousHealth.current === null) {
      previousHealth.current = data.health;
      previousStatus.current = data;
      return;
    }
    if (previousHealth.current !== data.health) {
      notifyHealthChange(data.health);
      previousHealth.current = data.health;
    }
    notifyTransitionExtras(previousStatus.current, data);
    previousStatus.current = data;
  }, [data]);

  const emergency = data?.emergencyUsage;
  const replay = data?.replaySummary;
  const mediaHealthLabel = !data
    ? "Unknown"
    : data.mounted && data.writable && health === "HEALTHY"
      ? "HEALTHY — SD present / mounted / writable"
      : health === "READ_ONLY"
        ? "READ ONLY — mounted, not writable"
        : health === "DEGRADED" || health === "WARNING" || health === "CRITICAL"
          ? `${copy.status} — see emergency storage`
          : copy.status;
  const rows = [
    ["Media health", mediaHealthLabel],
    ["SD present", data ? (data.sdPresent ? "Yes" : "No") : "Unknown"],
    ["SD mounted", data ? (data.mounted ? "Yes" : "No") : "Unknown"],
    [
      "Readable / writable",
      data ? `${data.readable ? "Yes" : "No"} / ${data.writable ? "Yes" : "No"}` : "Unknown",
    ],
    ["Capacity", formatBytes(data?.totalSpace)],
    ["Used", formatBytes(data?.usedSpace)],
    ["Free", formatBytes(data?.freeSpace)],
    ["Journal status", known(data?.journalHealthy)],
    ["Last successful write", formatAge(data?.lastWriteAgeSeconds)],
    [
      "Last successful backup",
      data?.lastSuccessfulBackup ||
        (data?.lastSuccessfulBackupAgeSeconds !== undefined &&
        data.lastSuccessfulBackupAgeSeconds !== null
          ? formatAge(data.lastSuccessfulBackupAgeSeconds)
          : "Not recorded since startup"),
    ],
    ["Last SD verification", formatAge(data?.lastSdVerificationAgeSeconds)],
    ["Last successful replay", formatAge(data?.lastSuccessfulReplayAgeSeconds)],
    [
      "Pending history replay",
      data?.pendingReplay === undefined ? "Unknown" : String(data.pendingReplay),
    ],
    [
      "Pending conflicts",
      data?.pendingConflicts === undefined ? "Unknown" : String(data.pendingConflicts),
    ],
    [
      "Reconciliation",
      data?.pendingConflicts
        ? `Attention required — ${data.pendingConflicts} unresolved conflict${data.pendingConflicts === 1 ? "" : "s"}`
        : data?.reconciliationStatus === "conflict"
          ? "Attention required — unresolved conflict"
          : "None",
    ],
    ["Retry state", data?.retryState ?? "Unknown"],
    [
      "Retry remaining",
      data?.retryRemaining === undefined ? "Unknown" : String(data.retryRemaining),
    ],
    ["Watch mode", data?.watchMode === undefined ? "Unknown" : data.watchMode ? "Yes" : "No"],
    [
      "Recovery mode",
      data?.recoveryMode === undefined ? "Unknown" : data.recoveryMode ? "Yes" : "No",
    ],
    ["Diagnostic cause", data?.diagnosticCause ?? data?.internalDiagnosticState ?? "Unknown"],
    ["CRC verification", known(data?.crcHealthy)],
    ["Recovery queue", data?.recoveryQueue === undefined ? "Unknown" : String(data.recoveryQueue)],
    ["Filesystem mount", data?.filesystemMount ?? "Unknown"],
    ["Current storage mode", data?.mode ?? data?.storageMode ?? "Unknown"],
    [
      "Last replay summary",
      replay
        ? `files=${replay.files?.length ?? 0} history=${replay.historyRecords ?? 0} skipped=${replay.skipped ?? 0} conflicts=${replay.conflicts ?? 0}`
        : "None",
    ],
  ];

  const statusLabel = isLoading ? "Loading" : isError ? "Unknown" : copy.status;
  const filesystemLabel = data?.filesystemMount ?? data?.mode ?? data?.storageMode ?? "Unknown";
  const advancedRows = rows.filter(
    ([label]) =>
      !["Capacity", "Used", "Free", "SD mounted", "Journal status", "Filesystem mount"].includes(
        label,
      ),
  );

  const alerts = (
    <>
      {data?.warnings?.length ? (
        <ul className="mt-3 space-y-1 rounded-md border border-amber-300 bg-amber-50 p-2 text-xs text-amber-900 dark:border-amber-800 dark:bg-amber-950/30 dark:text-amber-300">
          {data.warnings.map((warning) => (
            <li key={warning} className="flex items-start gap-1.5">
              <AlertTriangle className="mt-0.5 h-3.5 w-3.5 shrink-0" />
              <span>{warning}</span>
            </li>
          ))}
        </ul>
      ) : null}

      {data?.conflicts?.length ? (
        <div className="mt-3 space-y-1 rounded-md border border-orange-300 bg-orange-50 p-2 text-xs text-orange-900 dark:border-orange-800 dark:bg-orange-950/30 dark:text-orange-300">
          <p className="font-medium flex items-start gap-1.5">
            <AlertTriangle className="mt-0.5 h-3.5 w-3.5 shrink-0" />
            SPIFFS vs SD copy differs — owner review may be required for config files. Sales and
            portal sessions merge automatically on SD retry.
          </p>
          <ul className="space-y-1 pl-5 list-disc">
            {data.conflicts.map((conflict) => (
              <li key={`${conflict.path}-${conflict.generation ?? 0}`}>
                {conflict.path}
                {conflict.generation != null ? ` (gen ${conflict.generation})` : ""}
              </li>
            ))}
          </ul>
        </div>
      ) : null}
    </>
  );

  if (variant === "console") {
    return (
      <section className={cn("space-y-3", className)} aria-label="SD Card Health">
        <div className="flex items-center justify-between gap-3">
          <div className="flex items-center gap-2 text-[13px] font-semibold">
            <HardDrive className="h-4 w-4" />
            SD Card Health
          </div>
          {isLoading ? (
            <Skeleton className="h-5 w-20" />
          ) : (
            <ConfigStatusBadge label={statusLabel} tone={healthTone(health)} />
          )}
        </div>
        {isError ? (
          <div className="space-y-2 text-[13px] text-muted-foreground">
            <p>Unable to retrieve SD card health.</p>
            <Button type="button" size="sm" variant="outline" onClick={() => void refetch()}>
              Retry
            </Button>
          </div>
        ) : isLoading ? (
          <div className="grid grid-cols-2 gap-2 lg:grid-cols-4">
            {Array.from({ length: 4 }).map((_, i) => (
              <Skeleton key={i} className="h-14 w-full" />
            ))}
          </div>
        ) : (
          <>
            <p className="text-xs text-muted-foreground">{copy.description}</p>
            <div className="grid grid-cols-2 gap-2 lg:grid-cols-4">
              <MetricTile label="Capacity" value={formatBytes(data?.totalSpace)} />
              <MetricTile label="Used" value={formatBytes(data?.usedSpace)} />
              <MetricTile label="Free" value={formatBytes(data?.freeSpace)} />
              <MetricTile label="Status" value={statusLabel} />
            </div>
            <div className="grid grid-cols-2 gap-2 lg:grid-cols-4">
              <MetricTile
                label="Mounted"
                value={data ? (data.mounted ? "Yes" : "No") : "Unknown"}
              />
              <MetricTile
                label="Writable"
                value={data ? (data.writable ? "Yes" : "No") : "Unknown"}
              />
              <MetricTile label="Filesystem" value={filesystemLabel} />
              <MetricTile label="Journal" value={known(data?.journalHealthy)} />
            </div>
            {alerts}
            <Collapsible>
              <CollapsibleTrigger className="flex h-10 w-full items-center justify-between rounded-md border bg-muted/20 px-3 text-[13px] font-medium">
                Advanced SD Card Diagnostics
                <span className="flex items-center gap-1 text-xs text-muted-foreground">
                  Show details
                  <ChevronDown className="h-3.5 w-3.5" />
                </span>
              </CollapsibleTrigger>
              <CollapsibleContent className="space-y-3 pt-3">
                <dl className="divide-y rounded-md border bg-background px-3">
                  {advancedRows.map(([label, value]) => (
                    <div
                      key={label}
                      className="flex items-start justify-between gap-3 py-2 text-xs"
                    >
                      <dt className="text-muted-foreground">{label}</dt>
                      <dd className="text-right font-medium">{value}</dd>
                    </div>
                  ))}
                </dl>
                <div>
                  <div className="mb-1 flex justify-between text-xs">
                    <span className="text-muted-foreground">Emergency storage</span>
                    <span className="font-medium tabular-nums">
                      {emergency
                        ? `${emergency.percent}% · ${formatBytes(emergency.bytes)} / ${formatBytes(emergency.quotaBytes)}`
                        : "Unknown"}
                    </span>
                  </div>
                  <Progress value={emergency?.percent ?? 0} className="h-1.5" />
                </div>
              </CollapsibleContent>
            </Collapsible>
          </>
        )}
      </section>
    );
  }

  return (
    <section className={cn("rounded-md border bg-card p-3", className)} aria-label="SD Card Health">
      <div className="flex items-center justify-between gap-3">
        <div className="flex items-center gap-2 text-sm font-medium">
          <HardDrive className="h-4 w-4" />
          SD Card Health
        </div>
        <span
          className={cn("rounded border px-2 py-0.5 text-xs font-semibold", HEALTH_STYLE[health])}
        >
          {statusLabel}
        </span>
      </div>

      <p className="mt-2 text-xs text-muted-foreground">{copy.description}</p>
      <p className="mt-1 text-xs font-medium">{copy.action}</p>

      <dl className="mt-3 divide-y rounded-md border bg-background px-3">
        {rows.map(([label, value]) => (
          <div key={label} className="flex items-start justify-between gap-3 py-2 text-xs">
            <dt className="text-muted-foreground">{label}</dt>
            <dd className="text-right font-medium">{value}</dd>
          </div>
        ))}
      </dl>

      <div className="mt-3">
        <div className="mb-1 flex justify-between text-xs">
          <span className="text-muted-foreground">Emergency storage</span>
          <span className="font-medium tabular-nums">
            {emergency
              ? `${emergency.percent}% · ${formatBytes(emergency.bytes)} / ${formatBytes(emergency.quotaBytes)}`
              : "Unknown"}
          </span>
        </div>
        <Progress value={emergency?.percent ?? 0} className="h-1.5" />
      </div>

      {alerts}
    </section>
  );
}
