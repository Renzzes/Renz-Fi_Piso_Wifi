import type { CoinState, SystemStatus } from "@/types/api";

export type StatusTone = "ok" | "warn" | "bad" | "unknown" | "neutral";

export function mikrotikDisplay(
  mikrotik: SystemStatus["mikrotik"] | undefined,
  loading: boolean,
): {
  label: string;
  ok: boolean;
  host: string;
  connectivityLabel: string;
  connectivityOk: boolean;
} {
  if (loading) {
    return {
      label: "Loading...",
      ok: false,
      host: "Loading...",
      connectivityLabel: "Loading...",
      connectivityOk: false,
    };
  }
  if (!mikrotik) {
    return {
      label: "N/A",
      ok: false,
      host: "N/A",
      connectivityLabel: "N/A",
      connectivityOk: false,
    };
  }

  const host = mikrotik.host?.trim() ?? "";
  const configured = mikrotik.configured ?? Boolean(host);
  if (!host || !configured) {
    return {
      label: "Not Configured",
      ok: false,
      host: "Not Configured",
      connectivityLabel: "N/A",
      connectivityOk: false,
    };
  }

  const connectivity = (mikrotik.connectivity ?? "unknown").toLowerCase();
  let connectivityLabel = "Unknown";
  let connectivityOk = false;
  if (connectivity === "online") {
    connectivityLabel = "Online";
    connectivityOk = true;
  } else if (connectivity === "offline") {
    connectivityLabel = "Offline";
  }

  return {
    label: "Configured",
    ok: true,
    host,
    connectivityLabel,
    connectivityOk,
  };
}

export function hotspotDisplay(
  hotspot: SystemStatus["hotspot"] | undefined,
  loading: boolean,
): { label: string; ok: boolean } {
  if (loading) return { label: "Loading...", ok: false };
  if (!hotspot) return { label: "Unknown", ok: false };

  const status = (hotspot.status ?? (hotspot.ok ? "available" : "unknown")).toLowerCase();
  if (status === "available") return { label: "Available", ok: true };
  if (status === "unavailable") return { label: "Unavailable", ok: false };
  return { label: "Unknown", ok: false };
}

export function coinStateLabel(state: CoinState | string | undefined) {
  switch (state) {
    case "WAITING_FOR_ACTIVITY":
      return "Waiting For Activity";
    case "RESPONDING":
      return "Responding";
    case "NO_RECENT_ACTIVITY":
      return "No Recent Activity";
    case "FAULT":
      return "Fault";
    case "DISABLED":
    default:
      return "Disabled";
  }
}

export function coinStateOk(state: CoinState | string | undefined) {
  return (
    state === "RESPONDING" || state === "WAITING_FOR_ACTIVITY" || state === "NO_RECENT_ACTIVITY"
  );
}

export function coinHardwareTone(state: CoinState | string | undefined): StatusTone {
  switch (state) {
    case "RESPONDING":
    case "WAITING_FOR_ACTIVITY":
      return "ok";
    case "NO_RECENT_ACTIVITY":
      return "warn";
    case "FAULT":
      return "bad";
    case "DISABLED":
      return "neutral";
    default:
      return "unknown";
  }
}

export function coinDisplay(
  status: SystemStatus | undefined,
  diagState: string | undefined,
  diagPulses: string | undefined,
  loading: boolean,
): {
  featureLabel: string;
  hardwareLabel: string;
  hardwareState: CoinState | string | undefined;
  ok: boolean;
  pulses: string;
  pulsesToday: number | undefined;
  lastPulse: string;
  lastCoin: string;
  totalCoins: string;
  totalPulses: string;
} {
  if (loading) {
    return {
      featureLabel: "Loading...",
      hardwareLabel: "Loading...",
      hardwareState: undefined,
      ok: false,
      pulses: "Loading...",
      pulsesToday: undefined,
      lastPulse: "Loading...",
      lastCoin: "Loading...",
      totalCoins: "Loading...",
      totalPulses: "Loading...",
    };
  }

  const coin = status?.coinSlot;
  const enabled = coin?.enabled ?? coin?.ok ?? false;
  const pulsesToday =
    diagPulses !== undefined && diagPulses !== "" ? Number(diagPulses) : coin?.pulsesToday;
  const pulses =
    diagPulses ?? (coin?.pulsesToday !== undefined ? String(coin.pulsesToday) : undefined);
  const hardwareState = coin?.hardwareState ?? coin?.state;

  return {
    featureLabel: enabled ? "Enabled" : "Disabled",
    hardwareLabel: coinStateLabel(hardwareState) || diagState || coin?.stateLabel || "N/A",
    hardwareState,
    ok: enabled && coinStateOk(hardwareState),
    pulses: pulses !== undefined ? `${pulses} pulses today` : "N/A",
    pulsesToday: Number.isFinite(pulsesToday) ? pulsesToday : undefined,
    lastPulse: coin?.lastPulseTimestamp ?? "N/A",
    lastCoin: coin?.lastCoinTimestamp ?? "N/A",
    totalCoins: coin?.totalCoinCount !== undefined ? String(coin.totalCoinCount) : "N/A",
    totalPulses: coin?.totalPulseCount !== undefined ? String(coin.totalPulseCount) : "N/A",
  };
}

export function boolStatus(
  ok: boolean | undefined,
  loading: boolean,
  okLabel: string,
  failLabel: string,
): { label: string; ok: boolean } {
  if (loading) return { label: "Loading...", ok: false };
  if (ok === undefined) return { label: "N/A", ok: false };
  return { label: ok ? okLabel : failLabel, ok };
}

export function connectivityTone(ok: boolean, label: string): StatusTone {
  if (ok) return "ok";
  if (label === "Offline" || label === "Unavailable" || label === "Disconnected") return "bad";
  if (label === "Not Configured" || label === "Disabled") return "neutral";
  if (label === "No Recent Activity") return "warn";
  return "unknown";
}

export function healthLevelTone(level: string | undefined): StatusTone {
  if (level === "HEALTHY" || level === "ACTIVE_SESSION") return "ok";
  if (level === "WARNING") return "warn";
  if (level === "ERROR") return "bad";
  return "unknown";
}

export function healthLevelLabel(level: string | undefined): string {
  if (level === "HEALTHY" || level === "ACTIVE_SESSION") return "Healthy";
  if (level === "WARNING") return "Warning";
  if (level === "ERROR") return "Error";
  return level || "N/A";
}

export function coinRateLabel(
  settings: Record<string, string> | undefined,
  loading: boolean,
): string {
  if (loading) return "Loading...";
  if (!settings) return "N/A";
  const pulses = Number(settings.calibration ?? settings.pulsesPerPeso);
  if (!Number.isFinite(pulses) || pulses <= 0) return "N/A";
  if (pulses === 1) return "₱1";
  return `₱1 / ${pulses} pulses`;
}
