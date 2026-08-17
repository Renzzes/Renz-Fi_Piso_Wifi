import type { QueryClient } from "@tanstack/react-query";

/**
 * After Test Connection / Synchronize Router: router-cache is authoritative.
 * Invalidate + refetch every production view that reads it so banners and
 * metrics repaint from the same snapshot (no page reload).
 */
export async function refreshProductionRouterViews(queryClient: QueryClient): Promise<void> {
  await Promise.all([
    queryClient.invalidateQueries({ queryKey: ["router"] }),
    queryClient.invalidateQueries({ queryKey: ["system", "status"] }),
    queryClient.invalidateQueries({ queryKey: ["system", "health"] }),
    queryClient.invalidateQueries({ queryKey: ["system", "wifiConfig"] }),
  ]);
  await Promise.all([
    queryClient.refetchQueries({ queryKey: ["router", "cache"] }),
    queryClient.refetchQueries({ queryKey: ["router", "profiles"] }),
    queryClient.refetchQueries({ queryKey: ["router", "wireless"] }),
    queryClient.refetchQueries({ queryKey: ["system", "status"] }),
    queryClient.refetchQueries({ queryKey: ["system", "health"] }),
    queryClient.refetchQueries({ queryKey: ["system", "wifiConfig"] }),
  ]);
}
