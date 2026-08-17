import { useEffect, useRef } from "react";
import { apiUrl, embeddedApi } from "@/services/embeddedApi";
import type {
  InstallationAbortedEvent,
  InstallationCompletedEvent,
  InstallationStateChangedEvent,
  ProvisioningProgress,
} from "@/types/provisioning";

const RECONNECT_MS = 3000;

export type ProvisioningEventHandlers = {
  onProgress?: (payload: ProvisioningProgress) => void;
  onStateChanged?: (payload: InstallationStateChangedEvent) => void;
  onCompleted?: (payload: InstallationCompletedEvent) => void;
  onAborted?: (payload: InstallationAbortedEvent) => void;
  onConnectionChange?: (connected: boolean) => void;
};

function parsePayload<T>(event: MessageEvent): T | null {
  try {
    return JSON.parse(String(event.data)) as T;
  } catch {
    return null;
  }
}

/** Subscribe once to installation.* SSE events — updates ProvisioningContext via handlers. */
export function useProvisioningEvents(
  enabled: boolean,
  handlers: ProvisioningEventHandlers,
) {
  const handlersRef = useRef(handlers);
  handlersRef.current = handlers;

  useEffect(() => {
    if (!enabled) return;

    let es: EventSource | null = null;
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    let closed = false;

    const connect = () => {
      if (closed) return;
      es = new EventSource(apiUrl(embeddedApi.events), { withCredentials: true });

      es.onopen = () => {
        handlersRef.current.onConnectionChange?.(true);
      };

      es.onerror = () => {
        handlersRef.current.onConnectionChange?.(false);
        es?.close();
        if (!closed) {
          reconnectTimer = setTimeout(connect, RECONNECT_MS);
        }
      };

      es.addEventListener("installation.progress", (event) => {
        const payload = parsePayload<ProvisioningProgress>(event);
        if (payload) handlersRef.current.onProgress?.(payload);
      });

      es.addEventListener("installation.state_changed", (event) => {
        const payload = parsePayload<InstallationStateChangedEvent>(event);
        if (payload) handlersRef.current.onStateChanged?.(payload);
      });

      es.addEventListener("installation.completed", (event) => {
        const payload = parsePayload<InstallationCompletedEvent>(event);
        handlersRef.current.onCompleted?.(payload ?? {});
      });

      es.addEventListener("installation.aborted", (event) => {
        const payload = parsePayload<InstallationAbortedEvent>(event);
        handlersRef.current.onAborted?.(payload ?? {});
      });
    };

    connect();

    return () => {
      closed = true;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      es?.close();
      handlersRef.current.onConnectionChange?.(false);
    };
  }, [enabled]);
}
