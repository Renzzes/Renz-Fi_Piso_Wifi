import { useCallback, useRef } from "react";

/** Restore keyboard focus after async setup actions complete. */
export function useFocusRestore() {
  const lastFocused = useRef<HTMLElement | null>(null);

  const captureFocus = useCallback(() => {
    const active = document.activeElement;
    lastFocused.current = active instanceof HTMLElement ? active : null;
  }, []);

  const restoreFocus = useCallback(() => {
    const target = lastFocused.current;
    if (target && document.contains(target)) {
      target.focus({ preventScroll: true });
    }
  }, []);

  return { captureFocus, restoreFocus };
}
