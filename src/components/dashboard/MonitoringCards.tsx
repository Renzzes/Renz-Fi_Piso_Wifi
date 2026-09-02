import { useState } from "react";
import { Dialog, DialogContent, DialogHeader, DialogTitle } from "@/components/ui/dialog";
import { StorageHealthCard } from "@/components/StorageHealthCard";
import {
  CardError,
  CardLink,
  CardSkeleton,
  DashboardCard,
  DashboardCardHeader,
  MetricRing,
  RingStat,
  StatusBadge,
  StatusRow,
} from "@/components/dashboard/DashboardPrimitives";
import { StorageBar } from "@/components/dashboard/OpsCards";
import { coinHardwareTone, type StatusTone } from "@/lib/dashboardDisplay";
import { formatMb, formatStorageFromMb, formatTimeOfDay } from "@/lib/dashboardFormat";
import type { CoinState } from "@/types/api";

export type DashboardStatusRow = {
  label: string;
  value: string;
  tone: StatusTone;
  mono?: boolean;
};

export function SystemStatusCard({
  loading,
  error,
  onRetry,
  primary,
  extra,
}: {
  loading: boolean;
  error: boolean;
  onRetry: () => void;
  primary: DashboardStatusRow[];
  extra: DashboardStatusRow[];
}) {
  const [open, setOpen] = useState(false);
  if (loading && primary.length === 0) return <CardSkeleton rows={7} />;
  if (error && primary.length === 0) {
    return (
      <CardError
        title="System Status"
        message="Unable to retrieve system status."
        onRetry={onRetry}
      />
    );
  }
  return (
    <DashboardCard>
      <DashboardCardHeader
        title="System Status"
        action={
          <button
            type="button"
            onClick={() => setOpen(true)}
            className="rounded-sm text-[12px] font-medium text-primary hover:text-primary/80 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            Details
          </button>
        }
      />
      <div>
        {primary.map((row) => (
          <StatusRow key={row.label} {...row} />
        ))}
      </div>
      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent className="max-h-[85vh] overflow-y-auto sm:max-w-lg">
          <DialogHeader>
            <DialogTitle>System Status</DialogTitle>
          </DialogHeader>
          <div className="divide-y">
            {[...primary, ...extra].map((row) => (
              <StatusRow key={row.label} {...row} />
            ))}
          </div>
          <CardLink to="/router-status">Open Router Status</CardLink>
        </DialogContent>
      </Dialog>
    </DashboardCard>
  );
}

export function CoinSlotStatusCard({
  loading,
  error,
  onRetry,
  enabled,
  featureLabel,
  hardwareLabel,
  hardwareState,
  coinsToday,
  totalCoins,
  lastCoin,
  rateLabel,
}: {
  loading: boolean;
  error: boolean;
  onRetry: () => void;
  enabled: boolean;
  featureLabel: string;
  hardwareLabel: string;
  hardwareState: CoinState | string | undefined;
  coinsToday: number | undefined;
  totalCoins: string;
  lastCoin: string | undefined | null;
  rateLabel: string;
}) {
  if (loading && coinsToday === undefined) return <CardSkeleton rows={5} />;
  if (error && coinsToday === undefined && totalCoins === "N/A") {
    return (
      <CardError
        title="Coin Slot Status"
        message="Unable to retrieve coin slot status."
        onRetry={onRetry}
      />
    );
  }
  const today = coinsToday ?? 0;
  const decorativePct = today <= 0 ? 12 : Math.min(92, 18 + today * 4);
  const tone = coinHardwareTone(hardwareState);
  return (
    <DashboardCard>
      <DashboardCardHeader
        title="Coin Slot Status"
        action={<StatusBadge label={featureLabel} tone={enabled ? "ok" : "neutral"} />}
      />
      <MetricRing
        valueLabel={coinsToday === undefined ? "N/A" : String(today)}
        caption="coins today"
        percent={decorativePct}
        color="#F59E0B"
      />
      <p className="mt-2 text-center text-[12px] text-muted-foreground">
        <span className="sr-only">Coin hardware: </span>
        {hardwareLabel}
        {tone === "warn" ? " — no recent activity" : ""}
      </p>
      <div className="mt-4 grid grid-cols-3 gap-2 border-t pt-3">
        <RingStat value={totalCoins} label="total coins" />
        <RingStat value={formatTimeOfDay(lastCoin)} label="last coin" />
        <RingStat value={rateLabel} label="rate" />
      </div>
    </DashboardCard>
  );
}

export function SdCardHealthCard({
  loading,
  error,
  onRetry,
  badgeLabel,
  badgeTone,
  freePct,
  capacityLabel,
  uptimeLabel,
  mounted,
  flashUsed,
  flashTotal,
  flashPct,
  logsUsed,
  logsTotal,
  logsPct,
  sdReady,
  sdUsed,
  sdTotal,
  sdFree,
  sdPct,
  sdStatus,
}: {
  loading: boolean;
  error: boolean;
  onRetry: () => void;
  badgeLabel: string;
  badgeTone: StatusTone;
  freePct: number | undefined;
  capacityLabel: string;
  uptimeLabel: string;
  mounted: boolean;
  flashUsed?: number;
  flashTotal?: number;
  flashPct: number;
  logsUsed?: number;
  logsTotal?: number;
  logsPct: number;
  sdReady: boolean;
  sdUsed?: number;
  sdTotal?: number;
  sdFree?: number;
  sdPct: number;
  sdStatus: string;
}) {
  const [open, setOpen] = useState(false);
  if (loading && capacityLabel === "N/A") return <CardSkeleton rows={5} />;
  if (error && capacityLabel === "N/A") {
    return (
      <CardError
        title="SD Card Health"
        message="Unable to retrieve SD card health."
        onRetry={onRetry}
      />
    );
  }
  return (
    <DashboardCard>
      <DashboardCardHeader
        title="SD Card Health"
        action={
          <button
            type="button"
            onClick={() => setOpen(true)}
            className="rounded-full focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            aria-label={`SD card details, status ${badgeLabel}`}
          >
            <StatusBadge label={badgeLabel} tone={badgeTone} />
          </button>
        }
      />
      <MetricRing
        valueLabel={mounted && freePct !== undefined ? `${Math.round(freePct)}%` : "N/A"}
        caption="free space"
        percent={mounted && freePct !== undefined ? freePct : 8}
        color="#2FE0C4"
      />
      <div className="mt-4 grid grid-cols-3 gap-2 border-t pt-3">
        <RingStat value={capacityLabel} label="capacity" />
        <RingStat value="N/A" label="wear level" />
        <RingStat value={uptimeLabel} label="uptime" />
      </div>
      <div className="mt-4 space-y-2.5 border-t pt-3">
        <p className="text-[11px] font-medium uppercase tracking-wide text-muted-foreground">
          Storage usage
        </p>
        <StorageBar
          label="Flash (SPIFFS)"
          value={
            flashUsed === undefined ? "N/A" : `${formatMb(flashUsed)} / ${formatMb(flashTotal)} MB`
          }
          percent={flashPct}
          colorClass="bg-[#2FE0C4]"
        />
        <StorageBar
          label="Logs"
          value={logsUsed === undefined ? "N/A" : `${logsUsed} / ${logsTotal} KB`}
          percent={logsPct}
          colorClass="bg-[#F59E0B]"
        />
        <StorageBar
          label="SD Card"
          value={
            sdReady
              ? `${formatStorageFromMb(sdUsed)} / ${formatStorageFromMb(sdTotal)}`
              : sdStatus || "N/A"
          }
          percent={sdReady ? sdPct : 0}
          colorClass="bg-[#F59E0B]"
        />
        {sdReady ? (
          <p className="text-[11px] text-muted-foreground">
            Status: {sdStatus || "N/A"} • Free: {formatStorageFromMb(sdFree)}
          </p>
        ) : (
          <p className="text-[11px] text-muted-foreground">Status: {sdStatus || "N/A"}</p>
        )}
      </div>
      <div className="mt-3 text-right">
        <button
          type="button"
          onClick={() => setOpen(true)}
          className="text-[12px] font-medium text-primary hover:text-primary/80"
        >
          Details
        </button>
      </div>
      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent className="max-h-[85vh] overflow-y-auto sm:max-w-lg">
          <DialogHeader>
            <DialogTitle>SD Card Health</DialogTitle>
          </DialogHeader>
          <StorageHealthCard className="border-0 bg-transparent p-0 shadow-none" />
        </DialogContent>
      </Dialog>
    </DashboardCard>
  );
}
