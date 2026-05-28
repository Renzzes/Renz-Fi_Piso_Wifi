import { useQuery } from "@tanstack/react-query";
import { logsApi } from "@/services/logs";
import { useRealtime } from "@/contexts/RealtimeContext";

export function useLogs(q: string) {
  const { fallbackPollMs } = useRealtime();

  return useQuery({
    queryKey: ["logs", q],
    queryFn: () => logsApi.list(q || undefined),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });
}
