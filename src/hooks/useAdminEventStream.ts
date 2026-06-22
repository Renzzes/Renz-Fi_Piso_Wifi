import { useDashboardEvents } from "@/hooks/useDashboardEvents";

/** @deprecated Prefer useDashboardEvents — kept for existing imports. */
export function useAdminEventStream(enabled: boolean) {
  return useDashboardEvents(enabled);
}
