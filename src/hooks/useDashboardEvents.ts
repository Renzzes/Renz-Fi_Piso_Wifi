import { useEffect, useRef, useState } from "react";
import { useQueryClient } from "@tanstack/react-query";
import type { RealtimeContextValue } from "@/contexts/RealtimeContext";
import type { SystemStatus } from "@/types/api";
import { apiUrl, embeddedApi } from "@/services/embeddedApi";

const THROTTLE_MS = 500;
const RECONNECT_MS = 3000;
const FALLBACK_POLL_MS = 30_000;
const MAX_SEEN_SALE_IDS = 256;

const seenSaleIds = new Set<string>();

const EVENT_QUERY_MAP: Record<string, Array<{ queryKey: readonly unknown[] }>> = {
  "sales.changed": [
    { queryKey: ["system", "status"] },
    { queryKey: ["sales"] },
  ],
  // sale.created uses a targeted setQueryData patch for dashboard cards;
  // sales history list still invalidates lightly below.
  "sessions.changed": [
    { queryKey: ["system", "status"] },
    { queryKey: ["users", "active"] },
  ],
  "users.active": [
    { queryKey: ["users", "active"] },
    { queryKey: ["system", "status"] },
  ],
  "system.status": [{ queryKey: ["system", "status"] }],
  "storage.changed": [
    { queryKey: ["storage", "status"] },
    { queryKey: ["system", "status"] },
    { queryKey: ["system", "health"] },
  ],
  "rgb.changed": [
    { queryKey: ["rgb", "status"] },
    { queryKey: ["system", "rgb"] },
    { queryKey: ["system", "health"] },
    { queryKey: ["system", "status"] },
  ],
  "logs.changed": [{ queryKey: ["logs"] }],
  "log.entry": [{ queryKey: ["logs"] }],
  "portal.changed": [{ queryKey: ["portal"] }],
  "firmware.progress": [{ queryKey: ["firmware"] }],
  "sync.queue": [{ queryKey: ["system", "status"] }],
  "coin.diagnostics": [{ queryKey: ["coin", "diagnostics"] }],
  "coin.state.changed": [
    { queryKey: ["system", "coin"] },
    { queryKey: ["system", "health"] },
    { queryKey: ["system", "status"] },
  ],
  "coin.pulse": [
    { queryKey: ["system", "coin"] },
    { queryKey: ["coin", "diagnostics"] },
  ],
  "coin.accepted": [
    { queryKey: ["system", "coin"] },
    { queryKey: ["system", "health"] },
    { queryKey: ["coin", "diagnostics"] },
  ],
  "coin.fault": [
    { queryKey: ["system", "coin"] },
    { queryKey: ["system", "health"] },
  ],
  "vouchers.changed": [{ queryKey: ["vouchers"] }],
  "promos.changed": [{ queryKey: ["promos"] }],
};

function fallbackPollMs(sseConnected: boolean): RealtimeContextValue["fallbackPollMs"] {
  if (sseConnected) return false;
  return FALLBACK_POLL_MS;
}

function bumpBucket(
  bucket: { amount: number; sessions: number } | undefined,
  amount: number,
) {
  return {
    amount: (bucket?.amount ?? 0) + amount,
    sessions: (bucket?.sessions ?? 0) + 1,
  };
}

function rememberSaleId(id: string): boolean {
  if (!id || seenSaleIds.has(id)) return false;
  seenSaleIds.add(id);
  if (seenSaleIds.size > MAX_SEEN_SALE_IDS) {
    const first = seenSaleIds.values().next().value;
    if (first) seenSaleIds.delete(first);
  }
  return true;
}

/** Apply persisted sale.created to dashboard cards without a full reload. */
function applySaleCreatedPatch(
  queryClient: ReturnType<typeof useQueryClient>,
  raw: string,
): boolean {
  let amount = 0;
  let saleId = "";
  try {
    const parsed = JSON.parse(raw) as { amount?: unknown; id?: unknown };
    amount = Number(parsed.amount);
    saleId = typeof parsed.id === "string" ? parsed.id : "";
    if (!Number.isFinite(amount) || amount <= 0) return false;
    if (saleId && !rememberSaleId(saleId)) return false;
  } catch {
    return false;
  }

  queryClient.setQueryData<SystemStatus>(["system", "status"], (old) => {
    if (!old?.sales) return old;
    return {
      ...old,
      sales: {
        today: bumpBucket(old.sales.today, amount),
        weekly: bumpBucket(old.sales.weekly, amount),
        monthly: bumpBucket(old.sales.monthly, amount),
      },
      coinSlot: old.coinSlot
        ? {
            ...old.coinSlot,
            totalCoinCount:
              old.coinSlot.totalCoinCount !== undefined
                ? old.coinSlot.totalCoinCount + 1
                : old.coinSlot.totalCoinCount,
          }
        : old.coinSlot,
    };
  });
  return true;
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

      es.addEventListener("sale.created", (event: Event) => {
        const message = event as MessageEvent<string>;
        const patched = applySaleCreatedPatch(queryClient, message.data ?? "");
        if (!patched) {
          scheduleInvalidate("sales.changed");
          return;
        }
        // History/list pages only — dashboard cards already patched in RAM.
        pendingKeysRef.current.add(JSON.stringify(["sales"]));
        if (!flushTimerRef.current) {
          flushTimerRef.current = setTimeout(flushInvalidations, THROTTLE_MS);
        }
      });
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
    // When SSE is intentionally off (logout or factory-reset quiesce), do not
    // fall back to HTTP polling — that would recreate the Admin request herd.
    fallbackPollMs: enabled ? fallbackPollMs(connected) : false,
  };
}
