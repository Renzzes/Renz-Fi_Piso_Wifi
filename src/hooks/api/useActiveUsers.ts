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

export function useDisconnectUser() {
  const qc = useQueryClient();
  return useMutation({
    mutationFn: (mac: string) => usersApi.disconnect(mac),
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["users", "active"] });
      qc.invalidateQueries({ queryKey: ["system", "status"] });
    },
  });
}
