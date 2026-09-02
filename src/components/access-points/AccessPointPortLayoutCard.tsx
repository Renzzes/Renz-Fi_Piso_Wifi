import { useState } from "react";
import { cn } from "@/lib/utils";
import { ConfigCard } from "@/components/system-config/ConfigCard";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import {
  buildPortLayoutEntries,
  type EthernetPortSnapshot,
  type PortLayoutEntry,
} from "@/lib/mikrotikPortLayout";
import type { AccessPointRecord } from "@/services/accessPoints";
import type {
  NetworkAddressSnapshot,
  ValidatedApManagementNetwork,
} from "@/lib/apManagementIpRecommendations";
import { Dialog, DialogContent, DialogHeader, DialogTitle } from "@/components/ui/dialog";

function PortIndicator({ tone }: { tone: PortLayoutEntry["linkTone"] }) {
  const color =
    tone === "ok" ? "bg-emerald-500" : tone === "bad" ? "bg-red-500" : "bg-muted-foreground/50";
  return <span className={cn("inline-block h-2 w-2 rounded-full shrink-0", color)} aria-hidden />;
}

function PortDetailList({ fields }: { fields: PortLayoutEntry["details"] }) {
  return (
    <dl className="space-y-3 text-sm">
      {fields.map((field) => (
        <div
          key={field.label}
          className={cn(
            "grid gap-1 border-b border-border/60 pb-3 last:border-0 last:pb-0",
            field.multiline
              ? "grid-cols-1"
              : "grid-cols-1 sm:grid-cols-[9rem_minmax(0,1fr)] sm:gap-3",
          )}
        >
          <dt className="text-[12px] font-medium text-muted-foreground">{field.label}</dt>
          <dd
            className={cn(
              "min-w-0 break-words font-medium leading-snug",
              field.mono && "font-mono text-[12px] tabular-nums",
              field.multiline && "text-[13px] text-muted-foreground font-normal",
            )}
          >
            {field.value}
          </dd>
        </div>
      ))}
    </dl>
  );
}

export function AccessPointPortLayoutCard({
  boardName,
  identity,
  wanInterface,
  wanLink,
  detectedBridgePort,
  registeredAp,
  ethernetPorts,
  ethernetPortsKnown,
  networkAddresses,
  networkAddressesKnown,
  guestNetwork,
  guestGateway,
  guestDns,
  guestBridgeName,
  validatedManagement,
  lastObservedAt,
}: {
  boardName?: string;
  identity?: string;
  wanInterface?: string;
  wanLink?: string;
  detectedBridgePort?: string;
  registeredAp?: AccessPointRecord | null;
  ethernetPorts?: EthernetPortSnapshot[];
  ethernetPortsKnown?: boolean;
  networkAddresses?: NetworkAddressSnapshot[];
  networkAddressesKnown?: boolean;
  guestNetwork?: string;
  guestGateway?: string;
  guestDns?: string;
  guestBridgeName?: string;
  validatedManagement: ValidatedApManagementNetwork;
  lastObservedAt?: string;
}) {
  const [selectedPort, setSelectedPort] = useState<PortLayoutEntry | null>(null);
  const { ports, boardLabel, freshness } = buildPortLayoutEntries({
    boardName,
    identity,
    wanInterface,
    wanLink,
    detectedBridgePort,
    registeredAp,
    ethernetPorts,
    ethernetPortsKnown,
    networkAddresses,
    guestNetwork,
    guestGateway,
    guestDns,
    guestBridgeName,
    validatedManagement,
    lastObservedAt,
  });

  const freshnessLabel =
    freshness === "last_observed" && lastObservedAt
      ? `Last observed ${lastObservedAt} via Router Sync / Refresh`
      : "Interface state not available — run Router Sync / Refresh Router Information";

  return (
    <>
      <ConfigCard
        title="MikroTik Port Layout"
        description="Physical Ethernet ports with roles from RouterOS observations and registered AP data."
      >
        <div className="space-y-1">
          <p className="text-[12px] text-muted-foreground">{boardLabel}</p>
          <p className="text-[11px] text-muted-foreground">{freshnessLabel}</p>
          {networkAddressesKnown === false ? (
            <p className="text-[11px] text-amber-700 dark:text-amber-400">
              RouterOS IP address assignments not yet cached — AP management subnet cannot be
              validated until Router Sync completes.
            </p>
          ) : null}
        </div>

        {ports.length === 0 ? (
          <p className="text-[13px] text-muted-foreground">
            Port layout unavailable — router model not recognized from cache.
          </p>
        ) : (
          <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 xl:grid-cols-3 2xl:grid-cols-5">
            {ports.map((port) => (
              <button
                key={port.slot}
                type="button"
                className={cn(
                  "rounded-[12px] border bg-muted/10 p-3 text-left transition-colors hover:bg-muted/20 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring",
                  port.kind === "accessPoint" && "border-primary/40 bg-primary/5",
                  port.kind === "esp32" && "border-sky-500/30 bg-sky-500/5",
                )}
                onClick={() => setSelectedPort(port)}
              >
                <div className="flex items-start justify-between gap-2">
                  <span className="flex min-w-0 items-center gap-1.5 font-mono text-[13px] font-semibold">
                    <PortIndicator tone={port.linkTone} />
                    <span className="truncate">{port.routerOsName ?? port.slot}</span>
                  </span>
                  <ConfigStatusBadge label={port.linkLabel} tone={port.linkTone} />
                </div>

                <p className="mt-1.5 text-[12px] font-semibold">{port.role}</p>

                {port.cardLines.length > 0 ? (
                  <ul className="mt-2 space-y-0.5 text-[11px] text-muted-foreground leading-snug">
                    {port.cardLines.map((line) => (
                      <li key={line}>{line}</li>
                    ))}
                  </ul>
                ) : null}

                {port.setupSteps ? (
                  <div className="mt-3 rounded-md border border-dashed bg-background/60 p-2.5 text-left">
                    <p className="text-[10px] font-bold uppercase tracking-wide text-muted-foreground">
                      Access Point Setup
                    </p>
                    <ol className="mt-1.5 list-decimal space-y-1 pl-4 text-[11px] leading-snug text-muted-foreground">
                      {port.setupSteps.map((step) => (
                        <li key={step}>{step}</li>
                      ))}
                    </ol>
                  </div>
                ) : null}
              </button>
            ))}
          </div>
        )}
      </ConfigCard>

      <Dialog open={Boolean(selectedPort)} onOpenChange={(open) => !open && setSelectedPort(null)}>
        <DialogContent className="max-w-lg w-[min(100vw-1.5rem,32rem)] max-h-[min(90dvh,720px)] overflow-hidden flex flex-col gap-0 p-0">
          <DialogHeader className="shrink-0 border-b px-5 py-4 text-left">
            <DialogTitle className="font-mono text-base">
              {selectedPort?.routerOsName ?? selectedPort?.slot ?? "Port details"}
            </DialogTitle>
            {selectedPort ? (
              <p className="text-[12px] font-normal text-muted-foreground">{selectedPort.role}</p>
            ) : null}
          </DialogHeader>
          {selectedPort ? (
            <div className="min-h-0 flex-1 overflow-y-auto px-5 py-4">
              <PortDetailList fields={selectedPort.details} />
            </div>
          ) : null}
        </DialogContent>
      </Dialog>
    </>
  );
}
