import { createContext, useContext, type ReactNode } from "react";

export type RealtimeContextValue = {
  sseConnected: boolean;
  fallbackPollMs: number | false;
};

const defaultValue: RealtimeContextValue = {
  sseConnected: false,
  fallbackPollMs: 5000,
};

export const RealtimeContext = createContext<RealtimeContextValue>(defaultValue);

export function RealtimeProvider({
  value,
  children,
}: {
  value: RealtimeContextValue;
  children: ReactNode;
}) {
  return <RealtimeContext.Provider value={value}>{children}</RealtimeContext.Provider>;
}

export function useRealtime() {
  return useContext(RealtimeContext);
}
