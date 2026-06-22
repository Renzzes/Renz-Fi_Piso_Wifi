import { useEffect, useRef, useState } from "react";
import { useQueryClient } from "@tanstack/react-query";
import type { RealtimeContextValue } from "@/contexts/RealtimeContext";
import { apiUrl, embeddedApi } from "@/services/embeddedApi";

const THROTTLE_MS = 500;
const RECONNECT_MS = 3000;
const FALLBACK_POLL_MS = 30_000;

const EVENT_QUERY_MAP: Record<string, Array<{ queryKey: readonly unknown[] }>> = {
  "sales.changed": [
    { queryKey: ["system", "status"] },
    { queryKey: ["sales"] },
  ],
  "sessions.changed": [
    { queryKey: ["system", "status"] },
    { queryKey: ["users", "active"] },
  ],
  "users.active": [
    { queryKey: ["users", "active"] },
    { queryKey: ["system", "status"] },
  ],
  "system.status": [{ queryKey: ["system", "status"] }],
  "logs.changed": [{ queryKey: ["logs"] }],
  "log.entry": [{ queryKey: ["logs"] }],
  "portal.changed": [{ queryKey: ["portal"] }],
  "firmware.progress": [{ queryKey: ["firmware"] }],
  "sync.queue": [{ queryKey: ["system", "status"] }],
  "coin.diagnostics": [{ queryKey: ["coin", "diagnostics"] }],
  "vouchers.changed": [{ queryKey: ["vouchers"] }],
  "promos.changed": [{ queryKey: ["promos"] }],
};

function fallbackPollMs(sseConnected: boolean): RealtimeContextValue["fallbackPollMs"] {
  if (sseConnected) return false;
  return FALLBACK_POLL_MS;
}

export function useDashboardEvents(
  enabled: boolean,
): Pick<RealtimeContextValue, "sseConnected" | "sseReconnecting" | "fallbackPollMs"> {
  const queryClient = useQueryClient();
  const [connected, setConnected] = useState(false);
  const [reconnecting, setReconnecting] = useState(false);

  const pendingKeysRef = useRef(new Set<string>());
  const flushTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    if (!enabled) {
      setConnected(false);
      setReconnecting(false);
      return;
    }

    let es: EventSource | null = null;
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    let closed = false;

    const flushInvalidations = () => {
      flushTimerRef.current = null;
      const keys = pendingKeysRef.current;
      if (keys.size === 0) return;
      pendingKeysRef.current = new Set();
      for (const serialized of keys) {
        void queryClient.invalidateQueries({ queryKey: JSON.parse(serialized) as unknown[] });
      }
    };

    const scheduleInvalidate = (type: string) => {
      const targets = EVENT_QUERY_MAP[type];
      if (!targets) return;
      for (const target of targets) {
        pendingKeysRef.current.add(JSON.stringify(target.queryKey));
      }
      if (flushTimerRef.current) return;
      flushTimerRef.current = setTimeout(flushInvalidations, THROTTLE_MS);
    };

    const connect = () => {
      if (closed) return;
      setReconnecting(true);
      es = new EventSource(apiUrl(embeddedApi.events), { withCredentials: true });

      es.onopen = () => {
        setConnected(true);
        setReconnecting(false);
      };

      es.onerror = () => {
        setConnected(false);
        setReconnecting(true);
        es?.close();
        if (!closed) {
          reconnectTimer = setTimeout(connect, RECONNECT_MS);
        }
      };

      for (const type of Object.keys(EVENT_QUERY_MAP)) {
        es.addEventListener(type, () => scheduleInvalidate(type));
      }
    };

    connect();

    return () => {
      closed = true;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      if (flushTimerRef.current) clearTimeout(flushTimerRef.current);
      flushTimerRef.current = null;
      pendingKeysRef.current.clear();
      es?.close();
      setConnected(false);
      setReconnecting(false);
    };
  }, [enabled, queryClient]);

  return {
    sseConnected: connected,
    sseReconnecting: reconnecting,
    fallbackPollMs: fallbackPollMs(connected),
  };
}
