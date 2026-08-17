import { useCallback, useMemo, useState } from "react";
import { ChevronDown, ChevronUp } from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import type { FleetApplianceHealth } from "@/types/fleetHealth";
import { fleetHealthEmoji, fleetHealthLabel } from "@/types/fleetHealth";

type FleetApplianceCardProps = {
  health: FleetApplianceHealth;
  isActive: boolean;
  onSelect: () => void;
};

function formatLastSeen(iso: string | null | undefined): string {
  if (!iso) return "Never";
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return "Unknown";
  return date.toLocaleString();
}

export function FleetApplianceCard({ health, isActive, onSelect }: FleetApplianceCardProps) {
  const [expanded, setExpanded] = useState(false);
  const { device, level, score, warnings, snapshot } = health;

  return (
    <article
      className={cn(
        "rounded-lg border bg-card p-4 transition-colors",
        isActive && "border-primary/50 bg-primary/5 ring-1 ring-primary/20",
        !device.isOnline && "opacity-90",
      )}
    >
      <div className="flex flex-col sm:flex-row sm:items-start gap-3">
        <button
          type="button"
          className="flex flex-1 min-w-0 text-left gap-3"
          onClick={onSelect}
          disabled={!device.isOnline}
        >
          <span className="text-xl shrink-0" aria-hidden>
            {fleetHealthEmoji(level)}
          </span>
          <div className="min-w-0 flex-1">
            <div className="flex flex-wrap items-center gap-2">
              <h3 className="font-semibold truncate">{device.name}</h3>
              <span className="text-xs rounded-full bg-muted px-2 py-0.5">
                {fleetHealthLabel(level)}
              </span>
              <span className="text-xs text-muted-foreground">Score {score}</span>
            </div>
            <p className="text-xs text-muted-foreground font-mono truncate mt-0.5">
              {device.deviceId} · {device.ip}
            </p>
            <p className="text-xs text-muted-foreground mt-1 truncate">
              {device.firmwareVersion}
              {device.routerDriver ? ` · ${device.routerDriver}` : ""}
            </p>
            <div className="flex flex-wrap gap-2 mt-2 text-[11px] text-muted-foreground">
              <span>
                Storage:{" "}
                {snapshot?.storage.fallbackActive
                  ? "Fallback"
                  : snapshot?.storage.ok
                    ? "OK"
                    : "—"}
              </span>
              <span>
                Portal:{" "}
                {snapshot?.installation?.ready
                  ? snapshot?.portal?.hasBanner
                    ? "Ready"
                    : "Incomplete"
                  : "Setup"}
              </span>
              <span>
                Admin: {snapshot?.sessionAuthenticated ? "Session" : "No session"}
              </span>
            </div>
            <p className="text-[11px] text-muted-foreground mt-1">
              Last seen: {formatLastSeen(device.lastSeen)}
            </p>
          </div>
        </button>

        <div className="flex sm:flex-col gap-2 shrink-0">
          <Button
            type="button"
            size="sm"
            variant={isActive ? "secondary" : "default"}
            disabled={!device.isOnline}
            onClick={onSelect}
          >
            {isActive ? "Active" : "Switch"}
          </Button>
          <Button
            type="button"
            size="sm"
            variant="ghost"
            onClick={() => setExpanded((v) => !v)}
            aria-expanded={expanded}
          >
            {expanded ? (
              <>
                <ChevronUp className="h-4 w-4 mr-1" />
                Hide
              </>
            ) : (
              <>
                <ChevronDown className="h-4 w-4 mr-1" />
                Details
              </>
            )}
          </Button>
        </div>
      </div>

      {warnings.length > 0 ? (
        <ul className="mt-3 text-xs text-amber-700 dark:text-amber-400 space-y-1 list-disc pl-5">
          {warnings.map((w) => (
            <li key={w}>{w}</li>
          ))}
        </ul>
      ) : null}

      {expanded && snapshot ? (
        <dl className="mt-4 grid grid-cols-1 sm:grid-cols-2 gap-x-4 gap-y-2 text-xs border-t pt-3">
          <div>
            <dt className="text-muted-foreground">Serial</dt>
            <dd className="font-mono">{device.serialNumber || "—"}</dd>
          </div>
          <div>
            <dt className="text-muted-foreground">Hardware</dt>
            <dd>{device.hardwareRevision || "—"}</dd>
          </div>
          <div>
            <dt className="text-muted-foreground">Installation</dt>
            <dd>
              {snapshot.installation
                ? `${snapshot.installation.state} (${snapshot.installation.progressPercent}%)`
                : "—"}
            </dd>
          </div>
          <div>
            <dt className="text-muted-foreground">Router</dt>
            <dd>
              {snapshot.router?.configured
                ? `Configured (${snapshot.router.driverId ?? device.routerDriver ?? "—"})`
                : "Not configured"}
            </dd>
          </div>
          <div>
            <dt className="text-muted-foreground">Storage mode</dt>
            <dd>{snapshot.storage.storageMode ?? (snapshot.storage.ok ? "OK" : "Degraded")}</dd>
          </div>
          <div>
            <dt className="text-muted-foreground">SD / SPIFFS</dt>
            <dd>
              SD {snapshot.storage.sdMounted ? "mounted" : "—"} · SPIFFS{" "}
              {snapshot.storage.spiffsReady ? "ready" : "—"}
            </dd>
          </div>
          <div>
            <dt className="text-muted-foreground">Portal revision</dt>
            <dd>{snapshot.portal?.revision ?? 0}</dd>
          </div>
          <div>
            <dt className="text-muted-foreground">Coin</dt>
            <dd>
              {snapshot.coin?.enabled
                ? snapshot.coin.ok
                  ? "Ready"
                  : snapshot.coin.fault
                    ? "Fault"
                    : "Not ready"
                : "Disabled"}
            </dd>
          </div>
          <div>
            <dt className="text-muted-foreground">Uptime</dt>
            <dd>
              {snapshot.uptimeSeconds != null
                ? `${Math.floor(snapshot.uptimeSeconds / 3600)}h ${Math.floor((snapshot.uptimeSeconds % 3600) / 60)}m`
                : "—"}
            </dd>
          </div>
          <div>
            <dt className="text-muted-foreground">Capabilities</dt>
            <dd className="truncate">
              {device.capabilities
                ? Object.entries(device.capabilities)
                    .filter(([, v]) => v === true || (typeof v === "string" && v))
                    .map(([k]) => k)
                    .join(", ") || "—"
                : "—"}
            </dd>
          </div>
          {snapshot.build ? (
            <>
              <div>
                <dt className="text-muted-foreground">Build</dt>
                <dd className="font-mono truncate">
                  {snapshot.build.gitCommit ?? snapshot.build.firmwareVersion}
                </dd>
              </div>
              <div>
                <dt className="text-muted-foreground">Contracts</dt>
                <dd>
                  DP v{snapshot.build.deviceProfileVersion ?? "?"} · ST v
                  {snapshot.build.storageContractVersion ?? "?"} · HTTP v
                  {snapshot.build.httpContractVersion ?? "?"}
                </dd>
              </div>
            </>
          ) : null}
        </dl>
      ) : null}
    </article>
  );
}
