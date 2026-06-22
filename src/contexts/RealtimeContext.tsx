import { createContext, useContext, type ReactNode } from "react";

export type RealtimeContextValue = {
  sseConnected: boolean;
  sseReconnecting: boolean;
  fallbackPollMs: number | false;
  connectionLost: boolean;
  adminApiReachable: boolean;
};

const defaultValue: RealtimeContextValue = {
  sseConnected: false,
  sseReconnecting: false,
  fallbackPollMs: 5000,
  connectionLost: false,
  adminApiReachable: false,
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
