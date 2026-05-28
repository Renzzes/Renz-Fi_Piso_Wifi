import { useEffect, useState } from "react";
import { useQueryClient } from "@tanstack/react-query";
import type { RealtimeContextValue } from "@/contexts/RealtimeContext";
import { apiUrl, embeddedApi } from "@/services/embeddedApi";

const EVENT_QUERY_MAP: Record<string, Array<{ queryKey: readonly unknown[] }>> = {
  "system.status": [{ queryKey: ["system", "status"] }],
  "users.active": [{ queryKey: ["users", "active"] }],
  "logs.changed": [{ queryKey: ["logs"] }],
  "sync.queue": [{ queryKey: ["system", "status"] }],
  "coin.diagnostics": [{ queryKey: ["coin", "diagnostics"] }],
  "vouchers.changed": [{ queryKey: ["vouchers"] }],
  "sales.changed": [{ queryKey: ["sales"] }],
  "promos.changed": [{ queryKey: ["promos"] }],
};

const FALLBACK_POLL_MS = 30_000;

function fallbackPollMs(sseConnected: boolean): RealtimeContextValue["fallbackPollMs"] {
  if (sseConnected) return false;
  return FALLBACK_POLL_MS;
}

export function useAdminEventStream(enabled: boolean): RealtimeContextValue {
  const queryClient = useQueryClient();
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    if (!enabled) {
      setConnected(false);
      return;
    }

    let es: EventSource | null = null;
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    let closed = false;

    const invalidateForEvent = (type: string) => {
      const targets = EVENT_QUERY_MAP[type];
      if (!targets) return;
      for (const t of targets) {
        void queryClient.invalidateQueries({ queryKey: t.queryKey });
      }
    };

    const connect = () => {
      if (closed) return;
      es = new EventSource(apiUrl(embeddedApi.events), { withCredentials: true });

      es.onopen = () => setConnected(true);

      es.onerror = () => {
        setConnected(false);
        es?.close();
        if (!closed) {
          reconnectTimer = setTimeout(connect, 3000);
        }
      };

      for (const type of Object.keys(EVENT_QUERY_MAP)) {
        es.addEventListener(type, () => invalidateForEvent(type));
      }
    };

    connect();

    return () => {
      closed = true;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      es?.close();
      setConnected(false);
    };
  }, [enabled, queryClient]);

  return {
    sseConnected: connected,
    fallbackPollMs: fallbackPollMs(connected),
  };
}
