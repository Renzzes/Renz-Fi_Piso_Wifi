import { Progress } from "@/components/ui/progress";
import { SETUP_SCREEN_LABELS, type SetupScreenId } from "@/components/setup/stepRouter";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { useProvisioning } from "@/contexts/ProvisioningContext";
import { cn } from "@/lib/utils";

type WizardProgressBarProps = {
  currentScreen: SetupScreenId;
};

export function WizardProgressBar({ currentScreen }: WizardProgressBarProps) {
  const { installation, progress } = useProvisioning();

  const percent = progress?.percent ?? installation?.progressPercent ?? 0;
  const message = progress?.message?.trim();
  const stepLabel = SETUP_SCREEN_LABELS[currentScreen];

  return (
    <div className={wizardTheme.progress.container}>
      <div className="flex items-center justify-between gap-2 text-xs">
        <span className={wizardTheme.progress.labelActive}>{stepLabel}</span>
        <span className={wizardTheme.progress.labelMuted}>{percent}%</span>
      </div>
      <Progress
        value={percent}
        aria-label="Setup progress"
        className={cn(wizardTheme.progress.height, wizardTheme.progress.track)}
      />
      {message ? (
        <p className={wizardTheme.progress.message} title={message}>
          {message}
        </p>
      ) : null}
    </div>
  );
}
