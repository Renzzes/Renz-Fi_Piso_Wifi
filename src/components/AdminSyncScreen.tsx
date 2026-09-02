import { Check, Loader2, Radio, RefreshCw, Shield } from "lucide-react";
import { BrandLogo } from "@/components/BrandLogo";
import { ThemeToggle } from "@/components/ThemeToggle";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { ADMIN_SYNC_PHASES, type AdminSyncPhase } from "@/services/adminSync";
import { cn } from "@/lib/utils";

type AdminSyncScreenProps = {
  phase: AdminSyncPhase;
  error?: string | null;
  onRetry?: () => void;
};

export function AdminSyncScreen({ phase, error, onRetry }: AdminSyncScreenProps) {
  const activeIndex = ADMIN_SYNC_PHASES.findIndex((item) => item.id === phase);
  const progress =
    phase === "ready"
      ? 100
      : Math.max(8, Math.round(((activeIndex + 0.45) / ADMIN_SYNC_PHASES.length) * 100));

  return (
    <div className="relative flex min-h-screen items-center justify-center overflow-hidden bg-background px-4 py-8">
      <div
        aria-hidden
        className="pointer-events-none absolute inset-0 bg-primary/10 [mask-image:radial-gradient(ellipse_at_50%_0%,black,transparent_55%)]"
      />
      <div
        aria-hidden
        className="pointer-events-none absolute -left-24 top-24 h-56 w-56 rounded-full bg-primary/10 blur-3xl animate-pulse"
      />
      <div
        aria-hidden
        className="pointer-events-none absolute -right-16 bottom-16 h-48 w-48 rounded-full bg-primary/10 blur-3xl animate-pulse [animation-delay:700ms]"
      />

      <div className="absolute right-3 top-3 z-10">
        <ThemeToggle />
      </div>

      <Card className="relative z-10 w-full max-w-md border-border/80 bg-card/95 shadow-lg backdrop-blur-sm dark:shadow-[0_0_40px_rgba(37,99,235,0.12)]">
        <CardHeader className="items-center space-y-4 text-center">
          <div className="relative flex h-16 w-16 items-center justify-center">
            <span
              aria-hidden
              className="absolute inset-0 rounded-full border-2 border-primary/30 animate-[spin_3s_linear_infinite]"
            />
            <span
              aria-hidden
              className="absolute inset-1 rounded-full border border-dashed border-primary/40 animate-[spin_5s_linear_infinite] [animation-direction:reverse]"
            />
            <div className="relative flex h-12 w-12 items-center justify-center rounded-full bg-primary/10 ring-1 ring-primary/25">
              {error ? (
                <Shield className="h-5 w-5 text-destructive" />
              ) : phase === "ready" ? (
                <Check className="h-5 w-5 text-primary" />
              ) : (
                <Radio className="h-5 w-5 text-primary animate-pulse" />
              )}
            </div>
          </div>

          <BrandLogo height={48} className="mb-0" />

          <div className="space-y-1.5">
            <CardTitle className="text-2xl tracking-tight">
              {error ? "Connection interrupted" : "Connecting to Renz-Fi"}
              {!error && phase !== "ready" ? (
                <span className="inline-flex w-6 justify-start overflow-hidden">
                  <span className="animate-pulse">…</span>
                </span>
              ) : null}
            </CardTitle>
            <CardDescription className="text-pretty">
              Synchronizing the Admin client with appliance Core state. Router
              credentials stay on the ESP32 and are never sent to the browser.
            </CardDescription>
          </div>
        </CardHeader>

        <CardContent className="space-y-5">
          <div className="space-y-2">
            <div className="flex items-center justify-between text-[11px] uppercase tracking-wider text-muted-foreground">
              <span>Sync progress</span>
              <span className="tabular-nums text-foreground/80">{progress}%</span>
            </div>
            <div className="h-1.5 overflow-hidden rounded-full bg-muted">
              <div
                className={cn(
                  "h-full rounded-full bg-primary transition-[width] duration-500 ease-out",
                  !error && phase !== "ready" && "animate-pulse",
                )}
                style={{ width: `${progress}%` }}
              />
            </div>
          </div>

          <ol className="space-y-2">
            {ADMIN_SYNC_PHASES.map((item, index) => {
              const done = index < activeIndex || phase === "ready";
              const current = item.id === phase && phase !== "ready" && !error;
              return (
                <li
                  key={item.id}
                  className={cn(
                    "flex items-center gap-3 rounded-lg border px-3 py-2.5 text-sm transition-colors",
                    done && "border-primary/25 bg-primary/5 text-foreground",
                    current &&
                      "border-primary/40 bg-primary/10 text-foreground font-medium shadow-[0_0_0_1px_hsl(var(--primary)/0.12)]",
                    !done && !current && "border-transparent bg-muted/40 text-muted-foreground",
                  )}
                >
                  <span
                    className={cn(
                      "flex h-7 w-7 shrink-0 items-center justify-center rounded-full border",
                      done && "border-primary/40 bg-primary text-primary-foreground",
                      current && "border-primary/50 bg-background text-primary",
                      !done && !current && "border-border bg-background text-muted-foreground",
                    )}
                  >
                    {done ? (
                      <Check className="h-3.5 w-3.5" />
                    ) : current ? (
                      <Loader2 className="h-3.5 w-3.5 animate-spin" />
                    ) : (
                      <span className="h-1.5 w-1.5 rounded-full bg-current opacity-50" />
                    )}
                  </span>
                  <span className="min-w-0 flex-1">{item.label}</span>
                </li>
              );
            })}
          </ol>

          {error ? (
            <div className="space-y-3 rounded-lg border border-destructive/30 bg-destructive/5 px-3 py-3">
              <p className="text-sm text-destructive">{error}</p>
              {onRetry ? (
                <Button type="button" variant="outline" className="w-full gap-2" onClick={onRetry}>
                  <RefreshCw className="h-4 w-4" />
                  Retry synchronization
                </Button>
              ) : null}
            </div>
          ) : null}
        </CardContent>
      </Card>
    </div>
  );
}
