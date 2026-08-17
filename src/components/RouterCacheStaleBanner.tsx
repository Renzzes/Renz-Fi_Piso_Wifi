import { AlertTriangle, Loader2, RefreshCw } from "lucide-react";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import {
  formatRouterCacheAge,
  routerCacheLastSyncLabel,
  type RouterCacheStatusFields,
} from "@/lib/routerCacheStatus";

type RouterCacheStaleBannerProps = {
  cache: RouterCacheStatusFields | null | undefined;
  pending?: boolean;
  onRefresh: () => void;
  refreshLabel?: string;
};

export function RouterCacheStaleBanner({
  cache,
  pending = false,
  onRefresh,
  refreshLabel = "Refresh",
}: RouterCacheStaleBannerProps) {
  if (!cache?.populated || !cache.stale) return null;

  return (
    <Alert className="border-amber-500/40 bg-amber-500/5">
      <AlertTriangle className="h-4 w-4 text-amber-600" />
      <AlertTitle>Router information may be outdated</AlertTitle>
      <AlertDescription className="flex flex-col gap-2 sm:flex-row sm:items-center sm:justify-between">
        <span className="text-sm">
          Last synchronized {routerCacheLastSyncLabel(cache)}
          {cache.cacheAgeSeconds ? ` (${formatRouterCacheAge(cache.cacheAgeSeconds)})` : ""}.
          External RouterOS changes are not reflected until you synchronize.
        </span>
        <Button type="button" size="sm" variant="outline" disabled={pending} onClick={onRefresh}>
          {pending ? (
            <Loader2 className="h-4 w-4 animate-spin" />
          ) : (
            <RefreshCw className="h-4 w-4" />
          )}
          {refreshLabel}
        </Button>
      </AlertDescription>
    </Alert>
  );
}
