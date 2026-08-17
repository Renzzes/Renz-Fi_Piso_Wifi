import type { FleetPollIntervalMs } from "@/types/fleetHealth";
import { FLEET_POLL_INTERVALS_MS } from "@/types/fleetHealth";

const POLL_INTERVAL_KEY = "renz_fleet_poll_interval_ms";

export function readFleetPollIntervalMs(): FleetPollIntervalMs {
  try {
    const raw = localStorage.getItem(POLL_INTERVAL_KEY);
    const parsed = raw ? Number.parseInt(raw, 10) : NaN;
    if (FLEET_POLL_INTERVALS_MS.includes(parsed as FleetPollIntervalMs)) {
      return parsed as FleetPollIntervalMs;
    }
  } catch {
    /* ignore */
  }
  return 30_000;
}

export function writeFleetPollIntervalMs(ms: FleetPollIntervalMs): void {
  localStorage.setItem(POLL_INTERVAL_KEY, String(ms));
}
