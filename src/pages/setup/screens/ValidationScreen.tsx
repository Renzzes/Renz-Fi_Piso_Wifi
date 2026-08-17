import { useCallback, useState } from "react";
import { Button } from "@/components/ui/button";
import {
  SetupForm,
  SetupFormSection,
  SetupActions,
} from "@/components/setup/SetupForm";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { SetupStatusCard, SetupStatusList } from "@/components/setup/SetupStatusCard";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { useProvisioning } from "@/contexts/ProvisioningContext";
import type { SetupScreenProps } from "@/pages/setup/SetupScreenProps";
import {
  readValidationDraft,
  writeValidationDraft,
} from "@/pages/setup/validationDraft";
import { provisioningClient } from "@/services/provisioning/provisioningClient";
import { setupErrorMessage } from "@/pages/setup/setupErrorMessages";
import { useSetupMountOnce } from "@/hooks/setup/useSetupMountOnce";
import { useSetupSubmitGuard } from "@/hooks/setup/useSetupSubmitGuard";
import {
  parseValidationResults,
  validationCheckTargetScreen,
  validationGuidanceFor,
  validationResultToSetupStatus,
  type ValidationResult,
} from "@/models/ValidationResult";

export function ValidationScreen({ installation }: SetupScreenProps) {
  const draft = readValidationDraft();
  const {
    applyWorkflowData,
    setCurrentScreen,
    setLoading,
    clearError,
    progress,
  } = useProvisioning();
  const { isSubmitting, runExclusive } = useSetupSubmitGuard();
  const [results, setResults] = useState<ValidationResult[]>(
    draft.results.length > 0 ? draft.results : parseValidationResults(undefined),
  );
  const [evaluated, setEvaluated] = useState(draft.evaluated);
  const [canContinue, setCanContinue] = useState(draft.canContinue);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const runValidation = useCallback(async () => {
    await runExclusive(async () => {
      clearError();
      setErrorMessage(null);
      setLoading(true);

      try {
        const data = await provisioningClient.validate();
        const parsed = parseValidationResults(data.checks);
        const passed = data.passed === true && data.ok !== false;

        setResults(parsed);
        setEvaluated(true);
        setCanContinue(passed);

        writeValidationDraft({
          results: parsed,
          passed,
          canContinue: passed,
          evaluated: true,
        });

        if (passed) {
          applyWorkflowData(data);
        } else {
          setErrorMessage(data.error ?? "One or more installation checks failed.");
        }
      } catch (err) {
        setEvaluated(true);
        setCanContinue(false);
        setErrorMessage(setupErrorMessage(err, "Unable to run installation checks."));
      } finally {
        setLoading(false);
      }
    });
  }, [applyWorkflowData, clearError, runExclusive, setLoading]);

  useSetupMountOnce(() => {
    if (installation.state === "validation_passed" || installation.state === "ready") {
      setCurrentScreen("summary");
      return;
    }

    if (draft.evaluated && draft.results.length > 0) {
      setResults(draft.results);
      setEvaluated(true);
      setCanContinue(draft.canContinue);
      return;
    }

    void runValidation();
  });

  const handleContinue = () => {
    if (!canContinue) return;
    setCurrentScreen("summary");
  };

  const firstFailure = results.find((result) => !result.passed && result.severity === "error");

  return (
    <SetupForm aria-label="Installation verification">
      <div>
        <h2 className={wizardTheme.typography.title}>Installation verification</h2>
        <p className={wizardTheme.typography.description}>
          Running subsystem checks before finalizing this appliance.
        </p>
      </div>

      {progress?.message ? (
        <SetupInfoBanner variant="info" title="Running checks" description={progress.message} />
      ) : null}

      <SetupFormSection title="Validation results">
        <SetupStatusList aria-label="Installation validation checks">
          {results.map((result) => (
            <SetupStatusCard
              key={result.id}
              status={validationResultToSetupStatus(result, evaluated)}
              title={result.title}
              description={result.description}
              details={
                !result.passed && evaluated
                  ? validationGuidanceFor(result)
                  : undefined
              }
            />
          ))}
        </SetupStatusList>
      </SetupFormSection>

      {errorMessage ? (
        <SetupInfoBanner variant="error" title="Validation failed" description={errorMessage} />
      ) : null}

      {canContinue ? (
        <SetupInfoBanner
          variant="info"
          title="All checks passed"
          description="Continue to review the installation summary before finishing."
        />
      ) : null}

      {!canContinue && evaluated && firstFailure ? (
        <SetupInfoBanner
          variant="warning"
          title="Action required"
          description={
            validationGuidanceFor(firstFailure) ??
            "Resolve failed checks, then retry validation."
          }
        />
      ) : null}

      <SetupActions>
        <Button type="button" variant="outline" disabled={isSubmitting} onClick={() => void runValidation()}>
          Retry
        </Button>
        {firstFailure && validationCheckTargetScreen(firstFailure.id) ? (
          <Button
            type="button"
            variant="outline"
            onClick={() => setCurrentScreen(validationCheckTargetScreen(firstFailure.id)!)}
          >
            Go to step
          </Button>
        ) : null}
        <Button type="button" disabled={!canContinue || isSubmitting} onClick={handleContinue}>
          Continue
        </Button>
      </SetupActions>
    </SetupForm>
  );
}
