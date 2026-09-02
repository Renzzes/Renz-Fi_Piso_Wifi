import { useEffect, useState } from "react";

/** Append numeric samples into a fixed-length rolling buffer (truthful telemetry only). */
export function useRollingMetricHistory(
  value: number | undefined,
  maxPoints = 24,
  sampleTick?: number,
): number[] {
  const [history, setHistory] = useState<number[]>([]);

  useEffect(() => {
    if (value === undefined || !Number.isFinite(value)) return;
    setHistory((prev) => {
      const next = [...prev, value];
      if (next.length <= maxPoints) return next;
      return next.slice(next.length - maxPoints);
    });
  }, [value, maxPoints, sampleTick]);

  return history;
}
