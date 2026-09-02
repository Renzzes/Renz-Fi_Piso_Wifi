import { ConfigCard } from "@/components/system-config/ConfigCard";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import { InfoRow } from "@/components/system-config/InfoRow";
import type { AccessPointRecord } from "@/services/accessPoints";
import type { StatusTone } from "@/lib/dashboardDisplay";

function displayValue(value: string | undefined): string {
  const trimmed = value?.trim();
  return trimmed ? trimmed : "Unavailable";
}

function formatTimestamp(ms?: number | null): string {
  if (ms == null || !Number.isFinite(ms) || ms <= 0) return "Unavailable";
  // Firmware stores millis() since boot in lastCheckMs — not Unix epoch ms.
  if (ms < 1_000_000_000_000) {
    const minutes = Math.floor(ms / 60_000);
    if (minutes < 1) return "Since boot (< 1 min ago)";
    if (minutes < 60) return `Since boot (${minutes} min ago)`;
    const hours = Math.floor(minutes / 60);
    return `Since boot (${hours}h ${minutes % 60}m ago)`;
  }
  try {
    return new Intl.DateTimeFormat(undefined, {
      dateStyle: "medium",
      timeStyle: "short",
    }).format(new Date(ms));
  } catch {
    return "Unavailable";
  }
}

function statusLabel(status?: AccessPointRecord["status"]): string {
  switch (status) {
    case "online":
    case "network_reachable":
    case "management_reachable":
      return "Online";
    case "unreachable":
      return "Offline";
    case "disabled":
      return "Disabled";
    case "auth_failed":
      return "Auth failed";
    default:
      return "Unknown";
  }
}

function statusTone(status?: AccessPointRecord["status"]): StatusTone {
  switch (status) {
    case "online":
    case "network_reachable":
    case "management_reachable":
      return "ok";
    case "unreachable":
    case "auth_failed":
      return "bad";
    case "disabled":
      return "neutral";
    default:
      return "unknown";
  }
}

function vendorLabel(vendor: string): string {
  if (vendor === "tp-link") return "TP-Link";
  if (vendor === "ruijie") return "Ruijie";
  if (vendor === "tenda") return "Tenda";
  if (vendor === "other") return "Other";
  return "Generic";
}

export function AccessPointRegisteredSummaryCard({
  record,
  detectedBridgePort,
}: {
  record: AccessPointRecord | null;
  detectedBridgePort?: string;
}) {
  if (!record) {
    return (
      <ConfigCard title="Registered Access Point" description="No access point registered yet.">
        <p className="text-[13px] text-muted-foreground">
          Use Detect or Add Access Point after configuring the device in its manufacturer web
          interface.
        </p>
      </ConfigCard>
    );
  }

  const bridgePortLabel = detectedBridgePort?.trim()
    ? detectedBridgePort.trim()
    : "Not detected — run Detect";

  return (
    <ConfigCard
      title="Registered Access Point"
      description="Management metadata only — Renz-Fi does not configure the AP device."
    >
      <div className="flex flex-wrap items-center gap-2">
        <p className="text-[15px] font-semibold">{record.name || "Unnamed AP"}</p>
        <ConfigStatusBadge label={statusLabel(record.status)} tone={statusTone(record.status)} />
      </div>
      <div className="mt-2 rounded-md border bg-muted/20 px-3">
        <InfoRow label="Brand" value={vendorLabel(String(record.vendor))} />
        <InfoRow label="Model" value={displayValue(record.model)} />
        <InfoRow label="AP management IP" value={displayValue(record.managementIp)} mono />
        <InfoRow label="SSID label" value={displayValue(record.ssid)} />
        <InfoRow label="Registered" value={record.enabled ? "Yes" : "No"} />
        <InfoRow label="Registry status" value={statusLabel(record.status)} />
        <InfoRow
          label="Detected bridge port"
          value={bridgePortLabel}
          mono={Boolean(detectedBridgePort)}
        />
        <InfoRow label="Last check" value={formatTimestamp(record.lastCheck)} />
        <InfoRow
          label="Last successful check"
          value={formatTimestamp(record.lastSuccessfulCheck)}
        />
      </div>
    </ConfigCard>
  );
}
