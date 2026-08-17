import { useQuery } from "@tanstack/react-query";
import { useRealtime } from "@/contexts/RealtimeContext";
import { systemApi } from "@/services/system";

export function useStorageHealth() {
  const { fallbackPollMs } = useRealtime();

  return useQuery({
    queryKey: ["storage", "status"],
    queryFn: () => systemApi.storageStatus(),
    refetchInterval: fallbackPollMs,
    staleTime: 5_000,
    refetchIntervalInBackground: false,
  });
}
