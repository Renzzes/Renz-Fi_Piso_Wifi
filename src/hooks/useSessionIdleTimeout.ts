import { useCallback, useEffect, useRef, useState } from "react";
import {
  SESSION_IDLE_TIMEOUT_MS,
  SESSION_IDLE_WARNING_MS,
} from "@/config/session";
type UseSessionIdleTimeoutOptions = {
  enabled: boolean;
  onExpire: () => void;
};

export function useSessionIdleTimeout({ enabled, onExpire }: UseSessionIdleTimeoutOptions) {
  const [showWarning, setShowWarning] = useState(false);
  const lastActivityRef = useRef(Date.now());
  const expiredRef = useRef(false);
  const onExpireRef = useRef(onExpire);

  useEffect(() => {
    onExpireRef.current = onExpire;
  }, [onExpire]);

  const resetActivity = useCallback(() => {
    lastActivityRef.current = Date.now();
    expiredRef.current = false;
    setShowWarning(false);
  }, []);

  useEffect(() => {
    if (!enabled) {
      setShowWarning(false);
      return;
    }

    resetActivity();

    const onUserActivity = () => {
      resetActivity();
    };

    const events: Array<keyof WindowEventMap> = [
      "mousedown",
      "mousemove",
      "keydown",
      "click",
      "touchstart",
    ];

    for (const eventName of events) {
      window.addEventListener(eventName, onUserActivity, { passive: true });
    }

    const intervalId = window.setInterval(() => {
      const idleMs = Date.now() - lastActivityRef.current;

      if (idleMs >= SESSION_IDLE_TIMEOUT_MS) {
        if (!expiredRef.current) {
          expiredRef.current = true;
          setShowWarning(false);
          onExpireRef.current();
        }
        return;
      }

      setShowWarning(idleMs >= SESSION_IDLE_WARNING_MS);
    }, 1000);

    return () => {
      for (const eventName of events) {
        window.removeEventListener(eventName, onUserActivity);
      }
      window.clearInterval(intervalId);
      setShowWarning(false);
    };
  }, [enabled, resetActivity]);

  return {
    showWarning,
    stayLoggedIn: resetActivity,
  };
}
