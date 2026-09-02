import { createContext, useContext, type ReactNode } from "react";

export type RealtimeContextValue = {
  sseConnected: boolean;
  sseReconnecting: boolean;
  fallbackPollMs: number | false;
  connectionLost: boolean;
  adminApiReachable: boolean;
  /** Owner opt-in: EventSource + dashboard refetch. Off by default (standby). */
  liveUpdatesEnabled: boolean;
  setLiveUpdatesEnabled: (enabled: boolean) => void;
};

const noopSetLive = (_enabled: boolean) => {};

const defaultValue: RealtimeContextValue = {
  sseConnected: false,
  sseReconnecting: false,
  fallbackPollMs: false,
  connectionLost: false,
  adminApiReachable: false,
  liveUpdatesEnabled: false,
  setLiveUpdatesEnabled: noopSetLive,
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
