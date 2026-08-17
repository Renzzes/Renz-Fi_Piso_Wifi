import type { ReactNode } from "react";
import logo2Src from "../../../public/logo2.png";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { SetupCard } from "@/components/setup/SetupCard";
import { WizardProgressBar } from "@/components/setup/WizardProgressBar";
import { wizardClasses, wizardTheme } from "@/components/setup/WizardTheme";
import { SETUP_SCREEN_LABELS, type SetupScreenId } from "@/components/setup/stepRouter";

export type WizardShellProps = {
  currentScreen: SetupScreenId;
  children: ReactNode;
  loading?: boolean;
  error?: string | null;
  canGoBack?: boolean;
  canGoNext?: boolean;
  onBack?: () => void;
  onNext?: () => void;
  onCancel?: () => void;
  nextLabel?: string;
  showNext?: boolean;
};

export function WizardShell({
  currentScreen,
  children,
  loading = false,
  error,
  canGoBack = false,
  canGoNext = false,
  onBack,
  onNext,
  onCancel,
  nextLabel = "Next",
  showNext = true,
}: WizardShellProps) {
  const screenTitle = SETUP_SCREEN_LABELS[currentScreen];

  return (
    <div className={wizardClasses.shell}>
      <header className={wizardTheme.chrome.header}>
        <div className={wizardClasses.headerInner}>
          <div className="flex items-center gap-2 min-w-0">
            <img src={logo2Src} alt="Renz-Fi" className="h-8 w-8 shrink-0 rounded-md" />
            <div className="min-w-0">
              <p className="text-sm font-semibold leading-tight truncate">Setup Wizard</p>
              <p className={cn(wizardTheme.typography.meta, "truncate")}>{screenTitle}</p>
            </div>
          </div>
          {onCancel ? (
            <Button type="button" variant="ghost" size="sm" onClick={onCancel}>
              Cancel
            </Button>
          ) : null}
        </div>
        <div className={wizardClasses.headerProgress}>
          <WizardProgressBar currentScreen={currentScreen} />
        </div>
      </header>

      <main className={wizardClasses.content}>
        <SetupCard loading={loading}>{children}</SetupCard>

        {error ? (
          <p className="mt-3 text-sm text-destructive" role="alert">
            {error}
          </p>
        ) : null}
      </main>

      <footer className={wizardTheme.chrome.footer}>
        <div className={wizardClasses.footerInner}>
          <Button type="button" variant="outline" disabled={!canGoBack || loading} onClick={onBack}>
            Back
          </Button>
          {showNext ? (
            <Button type="button" disabled={!canGoNext || loading} onClick={onNext}>
              {nextLabel}
            </Button>
          ) : (
            <span />
          )}
        </div>
      </footer>
    </div>
  );
}
