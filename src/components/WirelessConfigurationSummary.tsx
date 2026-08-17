import { ConfigSection } from "@/components/ConfigSection";
import { StatusRow } from "@/components/StatCard";
import { Loader2, Wifi } from "lucide-react";
import {
  formatWirelessBand,
  wirelessConfigurationStatus,
  wirelessSourceLabel,
} from "@/lib/routerConfig";
import type { RouterWireless } from "@/services/router";

type WirelessConfigurationSummaryProps = {
  data?: RouterWireless | null;
  loading?: boolean;
  /** Fallback when band is not yet in router cache (MHz from production network verify). */
  frequencyFallback?: number | string | null;
};

function summaryRows(data: RouterWireless | null | undefined, frequencyFallback?: number | string | null) {
  const configured = data?.configured === true;
  const isNewAp = data?.wifiMode === "new";
  const ssid = data?.ssid?.trim() || "—";
  const iface = data?.interface?.trim() || "—";
  const band = formatWirelessBand(data?.band, frequencyFallback);

  const rows: Array<{ label: string; value: string; ok: boolean; variant?: "ok" | "unconfigured" | "unknown" }> = [
    {
      label: "Source",
      value: wirelessSourceLabel(data?.wifiMode),
      ok: configured,
      variant: configured ? "ok" : "unconfigured",
    },
  ];

  if (configured) {
    if (isNewAp) {
      rows.push({
        label: "SSID",
        value: ssid,
        ok: ssid !== "—",
      });
    } else {
      // Existing-router mode: interface name and production SSID are distinct.
      rows.push({
        label: "Configured Interface",
        value: iface,
        ok: iface !== "—",
      });
      rows.push({
        label: "Production SSID",
        value: ssid,
        ok: ssid !== "—",
      });
    }
    rows.push({
      label: "Band",
      value: band,
      ok: band !== "—",
      variant: band === "—" ? "unknown" : "ok",
    });
  }

  rows.push({
    label: "Status",
    value: wirelessConfigurationStatus(configured),
    ok: configured,
    variant: configured ? "ok" : "unconfigured",
  });

  return rows;
}

/** Read-only snapshot of the appliance wireless source, interface, SSID, band. */
export function WirelessConfigurationSummary({
  data,
  loading = false,
  frequencyFallback,
}: WirelessConfigurationSummaryProps) {
  const rows = summaryRows(data, frequencyFallback);

  return (
    <ConfigSection
      title="Wireless Configuration"
      description="What this appliance is using for customer Wi-Fi."
    >
      {loading ? (
        <p className="text-xs text-muted-foreground flex items-center gap-2">
          <Loader2 className="h-3.5 w-3.5 animate-spin" aria-hidden />
          Loading wireless configuration…
        </p>
      ) : (
        <div className="rounded-md border bg-muted/30 px-3">
          <div className="flex items-center gap-2 py-2 border-b text-sm text-muted-foreground">
            <Wifi className="h-4 w-4 shrink-0" aria-hidden />
            <span>Provisioned wireless profile</span>
          </div>
          {rows.map((row) => (
            <StatusRow
              key={row.label}
              label={row.label}
              status={row.value}
              ok={row.ok}
              variant={row.variant}
            />
          ))}
        </div>
      )}
    </ConfigSection>
  );
}
