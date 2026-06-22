import { RefreshCw, RotateCw, WifiOff } from "lucide-react";
import { Button } from "@/components/ui/button";

type ConnectionLostOverlayProps = {
  open: boolean;
  onRetry?: () => void;
  retrying?: boolean;
};

export function ConnectionLostOverlay({
  open,
  onRetry,
  retrying = false,
}: ConnectionLostOverlayProps) {
  if (!open) return null;

  return (
    <div
      className="fixed inset-0 z-[100] flex items-center justify-center bg-background/95 p-4 backdrop-blur-sm"
      role="alertdialog"
      aria-modal="true"
      aria-labelledby="connection-lost-title"
      aria-describedby="connection-lost-message"
    >
      <div className="w-full max-w-md rounded-lg border bg-card p-6 text-center shadow-lg">
        <div className="mx-auto mb-4 flex h-12 w-12 items-center justify-center rounded-full bg-red-500/10 text-red-600">
          <WifiOff className="h-6 w-6" aria-hidden="true" />
        </div>
        <h1 id="connection-lost-title" className="text-xl font-semibold">
          Connection Lost
        </h1>
        <p id="connection-lost-message" className="mt-2 text-sm text-muted-foreground">
          Unable to reach the Renz-Fi controller. Dashboard interaction is paused until the
          connection is restored.
        </p>
        <div className="mt-6 flex flex-col gap-2 sm:flex-row">
          <Button
            type="button"
            variant="outline"
            className="flex-1"
            disabled={retrying}
            onClick={() => onRetry?.()}
          >
            <RotateCw className={`h-4 w-4 ${retrying ? "animate-spin" : ""}`} />
            Retry
          </Button>
          <Button
            type="button"
            className="flex-1"
            onClick={() => window.location.reload()}
          >
            <RefreshCw className="h-4 w-4" />
            Refresh
          </Button>
        </div>
      </div>
    </div>
  );
}
