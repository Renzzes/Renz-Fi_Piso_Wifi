import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import type { LogEntry } from "@/types/api";
import { logsApi } from "@/services/logs";
import { apiUrl, embeddedApi } from "@/services/embeddedApi";

const MAX_ENTRIES = 500;
const RECONNECT_MS = 3000;

function normalizeEntry(raw: Partial<LogEntry>): LogEntry {
  return {
    id: raw.id,
    t: raw.t ?? "",
    lvl: raw.lvl ?? "INFO",
    type: raw.type ?? "system",
    msg: raw.msg ?? "",
  };
}

export function useLiveLogs(search: string) {
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [connected, setConnected] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);
  const stickToBottomRef = useRef(true);

  const appendEntry = useCallback((entry: LogEntry) => {
    setLogs((prev) => {
      const next = [...prev, entry];
      if (next.length > MAX_ENTRIES) next.splice(0, next.length - MAX_ENTRIES);
      return next;
    });
  }, []);

  useEffect(() => {
    let cancelled = false;
    logsApi
      .list()
      .then((rows) => {
        if (cancelled) return;
        const normalized = rows.map((r) => normalizeEntry(r));
        setLogs(normalized.slice(-MAX_ENTRIES));
      })
      .catch(() => {
        if (!cancelled) setLogs([]);
      });
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    let es: EventSource | null = null;
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    let closed = false;

    const connect = () => {
      if (closed) return;
      es = new EventSource(apiUrl(embeddedApi.events), { withCredentials: true });

      es.onopen = () => setConnected(true);
      es.onerror = () => {
        setConnected(false);
        es?.close();
        if (!closed) reconnectTimer = setTimeout(connect, RECONNECT_MS);
      };

      es.addEventListener("log.entry", (event) => {
        try {
          const raw = JSON.parse((event as MessageEvent).data) as Partial<LogEntry>;
          appendEntry(normalizeEntry(raw));
        } catch {
          /* ignore malformed payloads */
        }
      });

      es.addEventListener("logs.changed", () => {
        logsApi
          .list()
          .then((rows) => setLogs(rows.map((r) => normalizeEntry(r)).slice(-MAX_ENTRIES)))
          .catch(() => undefined);
      });
    };

    connect();

    return () => {
      closed = true;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      es?.close();
      setConnected(false);
    };
  }, [appendEntry]);

  const filtered = useMemo(() => {
    const q = search.trim().toLowerCase();
    if (!q) return logs;
    return logs.filter(
      (l) =>
        l.msg.toLowerCase().includes(q) ||
        l.type?.toLowerCase().includes(q) ||
        l.lvl.toLowerCase().includes(q),
    );
  }, [logs, search]);

  useEffect(() => {
    if (!stickToBottomRef.current || !containerRef.current) return;
    containerRef.current.scrollTop = containerRef.current.scrollHeight;
  }, [filtered.length]);

  const onScroll = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;
    const distance = el.scrollHeight - el.scrollTop - el.clientHeight;
    stickToBottomRef.current = distance < 48;
  }, []);

  const clear = useCallback(async () => {
    await logsApi.clear();
    setLogs([]);
  }, []);

  return {
    logs: filtered,
    totalCount: logs.length,
    connected,
    containerRef,
    onScroll,
    clear,
  };
}
