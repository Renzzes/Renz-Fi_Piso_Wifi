import { useEffect, useRef } from "react";

/** Run a mount effect once — avoids duplicate provisioning calls in React StrictMode. */
export function useSetupMountOnce(effect: () => void | (() => void)) {
  const ran = useRef(false);

  useEffect(() => {
    if (ran.current) return;
    ran.current = true;
    return effect();
  }, [effect]);
}
