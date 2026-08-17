import { useCallback, useEffect, useRef, useState } from "react";
import { authApi } from "@/services/auth";
import { markReloginRequired } from "@/services/sessionGate";

const HEALTH_POLL_MS = 5000;

type UseAdminApiMonitorOptions = {
  enabled: boolean;
  /** When EventBus SSE is open, skip /api/health polling — ping is liveness. */
  sseConnected?: boolean;
  onReconnectRequireLogin: () => void | Promise<void>;
};

export function useAdminApiMonitor({
  enabled,
  sseConnected = false,
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

    if (sseConnected) {
      return () => {
        window.removeEventListener("offline", onOffline);
      };
    }

    const intervalId = window.setInterval(() => void checkHealth(), HEALTH_POLL_MS);
    return () => {
      window.clearInterval(intervalId);
      window.removeEventListener("offline", onOffline);
    };
  }, [checkHealth, enabled, markLost, sseConnected]);

  return {
    connectionLost,
    adminApiReachable: enabled && !connectionLost,
    retryConnection: checkHealth,
  };
}
