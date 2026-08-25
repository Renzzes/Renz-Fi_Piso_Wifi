import { Loader2 } from "lucide-react";
import { ConfigCard } from "@/components/system-config/ConfigCard";
import { InfoRow } from "@/components/system-config/InfoRow";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import {
  formatWirelessBand,
  wirelessConfigurationStatus,
  wirelessSourceLabel,
} from "@/lib/routerConfig";
import type { RouterWireless } from "@/services/router";
import type { StatusTone } from "@/lib/dashboardDisplay";

type WirelessConfigurationSummaryProps = {
  data?: RouterWireless | null;
  loading?: boolean;
  error?: boolean;
  /** Fallback when band is not yet in router cache (MHz from production network verify). */
  frequencyFallback?: number | string | null;
};

function summaryRows(
  data: RouterWireless | null | undefined,
  frequencyFallback?: number | string | null,
) {
  const configured = data?.configured === true;
  const isNewAp = data?.wifiMode === "new";
  const ssid = data?.ssid?.trim() || "—";
  const iface = data?.interface?.trim() || "—";
  const band = formatWirelessBand(data?.band, frequencyFallback);

  const rows: Array<{
    label: string;
    value: string;
    tone?: StatusTone;
    mono?: boolean;
  }> = [
    {
      label: "Source",
      value: wirelessSourceLabel(data?.wifiMode),
    },
  ];

  if (configured) {
    if (isNewAp) {
      rows.push({
        label: "SSID",
        value: ssid,
        mono: true,
      });
    } else {
      rows.push({
        label: "Configured Interface",
        value: iface,
        mono: true,
      });
      rows.push({
        label: "Production SSID",
        value: ssid,
        mono: true,
      });
    }
    rows.push({
      label: "Band",
      value: band,
    });
  }

  rows.push({
    label: "Status",
    value: wirelessConfigurationStatus(configured),
    tone: configured ? "ok" : "unknown",
  });

  return rows;
}

/** Read-only snapshot of the appliance wireless source, interface, SSID, band. */
export function WirelessConfigurationSummary({
  data,
  loading = false,
  error = false,
  frequencyFallback,
}: WirelessConfigurationSummaryProps) {
  const rows = summaryRows(data, frequencyFallback);

  return (
    <ConfigCard
      title="Provisioned Wireless Profile"
      description="What this appliance is using for customer Wi-Fi."
      className="h-full"
    >
      {loading ? (
        <p className="flex items-center gap-2 text-xs text-muted-foreground">
          <Loader2 className="h-3.5 w-3.5 animate-spin" aria-hidden />
          Loading wireless configuration…
        </p>
      ) : error ? (
        <p className="text-xs text-destructive">Could not load cached wireless settings.</p>
      ) : (
        <div className="rounded-md border bg-muted/20 px-3">
          {rows.map((row) =>
            row.tone ? (
              <div
                key={row.label}
                className="flex items-center justify-between gap-3 border-b border-border/60 py-2 text-[13px] last:border-0"
              >
                <span className="text-muted-foreground">{row.label}</span>
                <ConfigStatusBadge label={row.value} tone={row.tone} />
              </div>
            ) : (
              <InfoRow key={row.label} label={row.label} value={row.value} mono={row.mono} />
            ),
          )}
        </div>
      )}
    </ConfigCard>
  );
}
