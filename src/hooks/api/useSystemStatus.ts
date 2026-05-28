import { useQuery } from "@tanstack/react-query";
import { systemApi } from "@/services/system";
import { useRealtime } from "@/contexts/RealtimeContext";

export function useSystemStatus() {
  const { fallbackPollMs } = useRealtime();

  return useQuery({
    queryKey: ["system", "status"],
    queryFn: () => systemApi.status(),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });
}
