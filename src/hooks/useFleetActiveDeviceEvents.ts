import { useEffect } from "react";
import { apiUrl, embeddedApi } from "@/services/embeddedApi";

const FLEET_SSE_EVENTS = [
  "installation.state_changed",
  "installation.completed",
  "installation.aborted",
  "router.connected",
  "router.disconnected",
  "router.unavailable",
  "portal.changed",
  "coin.fault",
  "coin.state.changed",
  "coin.accepted",
  "storage.changed",
] as const;

/**
 * Subscribes to SSE on the active appliance only.
 * Other registered appliances continue on polling interval.
 */
export function useFleetActiveDeviceEvents(
  enabled: boolean,
  activeDeviceId: string | null,
  onActiveDeviceEvent: () => void,
) {
  useEffect(() => {
    if (!enabled || !activeDeviceId) return;

    let es: EventSource | null = null;
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    let closed = false;

    const connect = () => {
      if (closed) return;
      es = new EventSource(apiUrl(embeddedApi.events), { withCredentials: true });

      es.onerror = () => {
        es?.close();
        if (!closed) {
          reconnectTimer = setTimeout(connect, 3000);
        }
      };

      for (const type of FLEET_SSE_EVENTS) {
        es.addEventListener(type, () => onActiveDeviceEvent());
      }
    };

    connect();

    return () => {
      closed = true;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      es?.close();
    };
  }, [enabled, activeDeviceId, onActiveDeviceEvent]);
}
