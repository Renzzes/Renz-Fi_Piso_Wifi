import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { usersApi } from "@/services/users";
import { useRealtime } from "@/contexts/RealtimeContext";

export function useActiveUsers() {
  const { fallbackPollMs } = useRealtime();

  return useQuery({
    queryKey: ["users", "active"],
    queryFn: () => usersApi.active(),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });
}

function invalidateUserQueries(qc: ReturnType<typeof useQueryClient>) {
  qc.invalidateQueries({ queryKey: ["users", "active"] });
  qc.invalidateQueries({ queryKey: ["system", "status"] });
}

export function useDisconnectUser() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (mac: string) => usersApi.disconnect(mac),
    onSuccess: () => invalidateUserQueries(qc),
  });
}

export function useReconnectUser() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (mac: string) => usersApi.reconnect(mac),
    onSuccess: () => invalidateUserQueries(qc),
  });
}

export function useTerminateUser() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (mac: string) => usersApi.terminate(mac),
    onSuccess: () => invalidateUserQueries(qc),
  });
}

export function usePauseUser() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (mac: string) => usersApi.pause(mac),
    onSuccess: () => invalidateUserQueries(qc),
  });
}

export function useResumeUser() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (mac: string) => usersApi.resume(mac),
    onSuccess: () => invalidateUserQueries(qc),
  });
}
