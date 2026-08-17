import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { ADMIN_SYNC_PHASES, type AdminSyncPhase } from "@/services/adminSync";

type AdminSyncScreenProps = {
  phase: AdminSyncPhase;
  error?: string | null;
  onRetry?: () => void;
};

export function AdminSyncScreen({ phase, error, onRetry }: AdminSyncScreenProps) {
  const activeIndex = ADMIN_SYNC_PHASES.findIndex((item) => item.id === phase);

  return (
    <div className="min-h-screen bg-muted/40 px-4 py-8 flex items-center justify-center">
      <Card className="w-full max-w-md">
        <CardHeader className="text-center">
          <CardTitle className="text-2xl">Connecting to Renz-Fi…</CardTitle>
          <CardDescription>
            Synchronizing the Admin client with appliance Core state. Router
            credentials stay on the ESP32 and are never sent to the browser.
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-3">
          <ol className="space-y-2 text-sm">
            {ADMIN_SYNC_PHASES.map((item, index) => {
              const done = index < activeIndex || phase === "ready";
              const current = item.id === phase && phase !== "ready";
              return (
                <li
                  key={item.id}
                  className={
                    done
                      ? "text-foreground"
                      : current
                        ? "text-foreground font-medium"
                        : "text-muted-foreground"
                  }
                >
                  {done ? "✓ " : current ? "→ " : "  "}
                  {item.label}
                </li>
              );
            })}
          </ol>
          {error ? (
            <div className="space-y-2">
              <p className="text-sm text-destructive">{error}</p>
              {onRetry ? (
                <button type="button" className="text-sm underline" onClick={onRetry}>
                  Retry synchronization
                </button>
              ) : null}
            </div>
          ) : null}
        </CardContent>
      </Card>
    </div>
  );
}
