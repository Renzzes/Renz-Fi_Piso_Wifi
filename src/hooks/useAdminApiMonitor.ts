import { useCallback, useEffect, useRef, useState } from "react";
import { authApi } from "@/services/auth";
import { markReloginRequired } from "@/services/sessionGate";

const HEALTH_POLL_MS = 5000;

type UseAdminApiMonitorOptions = {
  enabled: boolean;
  /** When EventBus SSE is open, skip /api/health polling — ping is liveness. */
  sseConnected?: boolean;
  /**
   * Standby Admin (live updates off): no periodic /api/health.
   * One check on enable + when the tab becomes visible again.
   */
  standbyIdle?: boolean;
  onReconnectRequireLogin: () => void | Promise<void>;
};

export function useAdminApiMonitor({
  enabled,
  sseConnected = false,
  standbyIdle = false,
  onReconnectRequireLogin,
}: UseAdminApiMonitorOptions) {
  const [connectionLost, setConnectionLost] = useState(false);
  const hadLossRef = useRef(false);
  const reconnectingRef = useRef(false);

  const markLost = useCallback(() => {
    hadLossRef.current = true;
    markReloginRequired("reconnect");
    setConnectionLost(true);
  }, []);

  const checkHealth = useCallback(async () => {
    if (!enabled || reconnectingRef.current) return;

    try {
      const json = await authApi.health();
      if (json.transientLoad) return;
      if (!json?.ok) throw new Error("Health check failed");

      if (hadLossRef.current) {
        reconnectingRef.current = true;
        hadLossRef.current = false;
        setConnectionLost(false);
        try {
          await onReconnectRequireLogin();
        } finally {
          reconnectingRef.current = false;
        }
        return;
      }

      setConnectionLost(false);
    } catch {
      if (enabled) markLost();
    }
  }, [enabled, markLost, onReconnectRequireLogin]);

  useEffect(() => {
    if (!enabled) {
      hadLossRef.current = false;
      setConnectionLost(false);
      return;
    }

    void checkHealth();

    const onOffline = () => {
      if (enabled) markLost();
    };
    window.addEventListener("offline", onOffline);

    const onVisible = () => {
      if (document.visibilityState === "visible") void checkHealth();
    };
    document.addEventListener("visibilitychange", onVisible);

    if (sseConnected || standbyIdle) {
      return () => {
        window.removeEventListener("offline", onOffline);
        document.removeEventListener("visibilitychange", onVisible);
      };
    }

    const intervalId = window.setInterval(() => void checkHealth(), HEALTH_POLL_MS);
    return () => {
      window.clearInterval(intervalId);
      window.removeEventListener("offline", onOffline);
      document.removeEventListener("visibilitychange", onVisible);
    };
  }, [checkHealth, enabled, markLost, sseConnected, standbyIdle]);

  return {
    connectionLost,
    adminApiReachable: enabled && !connectionLost,
    retryConnection: checkHealth,
  };
}
