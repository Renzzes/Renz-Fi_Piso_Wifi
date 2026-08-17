import { useCallback, useEffect, useRef, useState } from "react";

const OFFLINE_MESSAGE =
  "You appear to be offline. Setup progress is saved — reconnect and retry.";

/** Tracks browser online/offline without a new context provider. */
export function useOnlineStatus() {
  const [online, setOnline] = useState(
    typeof navigator === "undefined" ? true : navigator.onLine,
  );

  useEffect(() => {
    const onOnline = () => setOnline(true);
    const onOffline = () => setOnline(false);
    window.addEventListener("online", onOnline);
    window.addEventListener("offline", onOffline);
    return () => {
      window.removeEventListener("online", onOnline);
      window.removeEventListener("offline", onOffline);
    };
  }, []);

  return {
    online,
    offlineMessage: OFFLINE_MESSAGE,
  };
}
