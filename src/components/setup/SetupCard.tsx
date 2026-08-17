import type { ReactNode } from "react";
import { Loader2 } from "lucide-react";
import { Card, CardContent } from "@/components/ui/card";
import { cn } from "@/lib/utils";
import { wizardClasses, wizardTheme } from "@/components/setup/WizardTheme";

export type SetupCardProps = {
  children: ReactNode;
  className?: string;
  loading?: boolean;
};

/** Consistent step surface — all screen content renders inside SetupCard. */
export function SetupCard({ children, className, loading = false }: SetupCardProps) {
  return (
    <Card
      className={cn(
        wizardTheme.card.radius,
        wizardTheme.card.border,
        wizardTheme.card.overflow,
        className,
      )}
    >
      {loading ? <SetupCardLoadingOverlay /> : null}
      <CardContent className={wizardTheme.card.padding}>{children}</CardContent>
    </Card>
  );
}

export function SetupCardLoadingOverlay() {
  return (
    <div
      className={cn(
        "absolute inset-0 flex items-center justify-center",
        wizardTheme.overlay.background,
        wizardTheme.overlay.zIndex,
      )}
      aria-live="polite"
      aria-busy="true"
    >
      <Loader2
        className={cn("h-6 w-6", wizardTheme.motion.spin, wizardTheme.overlay.spinner)}
      />
    </div>
  );
}

export function SetupBootstrapScreen({
  message = "Loading installation status…",
}: {
  message?: string;
}) {
  return (
    <div className={wizardClasses.bootstrap}>
      <Loader2
        className={cn("h-8 w-8", wizardTheme.motion.spin, wizardTheme.overlay.spinner)}
      />
      <p className={wizardTheme.typography.description}>{message}</p>
    </div>
  );
}
