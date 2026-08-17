import { useCallback, useRef, useState } from "react";

/** Prevents duplicate form submissions during in-flight provisioning calls. */
export function useSetupSubmitGuard() {
  const lockRef = useRef(false);
  const [isSubmitting, setIsSubmitting] = useState(false);

  const runExclusive = useCallback(
    async <T>(action: () => Promise<T>): Promise<T | undefined> => {
      if (lockRef.current) return undefined;
      lockRef.current = true;
      setIsSubmitting(true);
      try {
        return await action();
      } finally {
        lockRef.current = false;
        setIsSubmitting(false);
      }
    },
    [],
  );

  return { isSubmitting, runExclusive };
}
