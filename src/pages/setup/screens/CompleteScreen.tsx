import { useCallback, useState } from "react";
import { useNavigate } from "react-router-dom";
import { Button } from "@/components/ui/button";
import {
  SetupForm,
  SetupFormField,
  SetupFormSection,
  SetupReadOnlyValue,
  SetupActions,
} from "@/components/setup/SetupForm";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { SetupStatusCard } from "@/components/setup/SetupStatusCard";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { cn } from "@/lib/utils";
import { useProvisioning } from "@/contexts/ProvisioningContext";
import type { SetupScreenProps } from "@/pages/setup/SetupScreenProps";
import { readValidationDraft } from "@/pages/setup/validationDraft";
import { clearApplianceConfigDraft } from "@/pages/setup/applianceConfigDraft";
import { clearValidationDraft } from "@/pages/setup/validationDraft";
import { clearPersistedSetupSession } from "@/pages/setup/setupSessionRecovery";
import { provisioningClient } from "@/services/provisioning/provisioningClient";
import { systemApi } from "@/services/system";
import { setupErrorMessage } from "@/pages/setup/setupErrorMessages";
import { productionNetworkReasonLabel } from "@/lib/productionNetworkReason";
import { useSetupMountOnce } from "@/hooks/setup/useSetupMountOnce";
import { useSetupSubmitGuard } from "@/hooks/setup/useSetupSubmitGuard";
import {
  buildInstallationReport,
  formatCoinSummaryLine,
  formatPortalSummaryLine,
  formatReportValue,
  formatRouterSummaryLine,
} from "@/models/InstallationReport";
import type { FinishInstallationSummary } from "@/types/routerProvisioning";

type ManagementWifiPolicy = "disable" | "keep_enabled";

function ManagementWifiOption({
  value,
  selected,
  label,
  description,
  onSelect,
}: {
  value: ManagementWifiPolicy;
  selected: boolean;
  label: string;
  description: string;
  onSelect: (value: ManagementWifiPolicy) => void;
}) {
  return (
    <label
      className={cn(
        "flex cursor-pointer items-start gap-3 rounded-lg border p-3 md:p-4",
        selected ? "border-primary bg-primary/5" : "border-border bg-card",
      )}
    >
      <input
        type="radio"
        name="management-wifi-policy"
        value={value}
        checked={selected}
        onChange={() => onSelect(value)}
        className="mt-1"
      />
      <span className="space-y-1">
        <span className="block text-sm font-medium">{label}</span>
        <span className="block text-xs text-muted-foreground leading-relaxed">
          {description}
        </span>
      </span>
    </label>
  );
}

export function CompleteScreen({ installation, session, workflowStep }: SetupScreenProps) {
  const navigate = useNavigate();
  const {
    applyWorkflowData,
    setCurrentScreen,
    setLoading,
    clearError,
    progress,
    sseConnected,
    startOver,
  } = useProvisioning();
  const { runExclusive } = useSetupSubmitGuard();
  const validationDraft = readValidationDraft();
  const report = buildInstallationReport({
    installation,
    session,
    workflowStep,
    validation: validationDraft.results,
  });
  const { summary } = report;

  const [finished, setFinished] = useState(installation.ready === true);
  const [finishSummary, setFinishSummary] = useState<FinishInstallationSummary | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [managementWifi, setManagementWifi] = useState<ManagementWifiPolicy>("disable");
  const [finishing, setFinishing] = useState(false);

  const runFinish = useCallback(async () => {
    await runExclusive(async () => {
      clearError();
      setErrorMessage(null);
      setFinishing(true);
      setLoading(true);

      try {
        const data = await provisioningClient.finish();
        if (data.finished && data.ok !== false) {
          await systemApi.managementApPostSetup(managementWifi === "keep_enabled");
          setFinished(true);
          setFinishSummary(data.summary ?? null);
          applyWorkflowData(data);
        } else {
          const reasonLabel = data.reason
            ? productionNetworkReasonLabel(data.reason)
            : null;
          setErrorMessage(
            reasonLabel
              ? `${reasonLabel}${data.error ? `\n\n${data.error}` : ""}`
              : (data.error ?? "Unable to finalize installation."),
          );
        }
      } catch (err) {
        setErrorMessage(setupErrorMessage(err, "Unable to finalize installation."));
      } finally {
        setFinishing(false);
        setLoading(false);
      }
    });
  }, [applyWorkflowData, clearError, managementWifi, runExclusive, setLoading]);

  useSetupMountOnce(() => {
    if (installation.ready === true) {
      setFinished(true);
    }
  });

  const handleDashboard = () => {
    navigate("/dashboard", { replace: true });
  };

  const handleRestart = async () => {
    clearValidationDraft();
    clearApplianceConfigDraft();
    clearPersistedSetupSession();
    await startOver();
  };

  const firmwareLabel =
    finishSummary?.firmwareVersion ?? formatReportValue(summary.applianceFirmwareVersion);

  return (
    <SetupForm aria-label="Installation complete">
      <div>
        <h2 className={wizardTheme.typography.title}>Installation complete</h2>
        <p className={wizardTheme.typography.description}>
          {finished
            ? "This Renz-Fi appliance is ready for production use."
            : finishing
              ? "Finalizing installation…"
              : "Choose Management Wi-Fi behavior, then complete setup."}
        </p>
      </div>

      {!finished && !errorMessage ? (
        <SetupFormSection title="Management Wi-Fi">
          <div className="space-y-3" role="radiogroup" aria-label="Management Wi-Fi">
            <ManagementWifiOption
              value="disable"
              selected={managementWifi === "disable"}
              label="Disable after setup (Recommended)"
              description="The Management Wi-Fi will automatically turn off after installation. You can enable it again later from the Owner App or Maintenance Mode."
              onSelect={setManagementWifi}
            />
            <ManagementWifiOption
              value="keep_enabled"
              selected={managementWifi === "keep_enabled"}
              label="Keep enabled"
              description="Recommended only if this appliance will be managed frequently."
              onSelect={setManagementWifi}
            />
          </div>
        </SetupFormSection>
      ) : null}

      {progress?.message ? (
        <SetupInfoBanner variant="info" title="Finalizing" description={progress.message} />
      ) : null}

      {finished ? (
        <>
          <SetupStatusCard
            status="success"
            title="Installation successful"
            description="Appliance ready"
            details={
              finished
                ? sseConnected
                  ? "Setup finalized · event channel connected"
                  : "Setup finalized"
                : undefined
            }
          />

          <SetupInfoBanner
            variant="info"
            title="Setup finished"
            description="You can manage promos, assets, and reports from the Admin Dashboard."
          />

          <SetupFormSection title="Summary">
            <SetupFormField label="Elapsed time">
              <SetupReadOnlyValue value={formatReportValue(summary.elapsedTime)} />
            </SetupFormField>
            <SetupFormField label="Router">
              <SetupReadOnlyValue value={formatRouterSummaryLine(summary)} />
            </SetupFormField>
            <SetupFormField label="Portal">
              <SetupReadOnlyValue value={formatPortalSummaryLine(summary)} />
            </SetupFormField>
            <SetupFormField label="Coin">
              <SetupReadOnlyValue value={formatCoinSummaryLine(summary)} />
            </SetupFormField>
            <SetupFormField label="Firmware">
              <SetupReadOnlyValue value={firmwareLabel} />
            </SetupFormField>
          </SetupFormSection>
        </>
      ) : null}

      {errorMessage ? (
        <>
          <SetupInfoBanner variant="error" title="Finish failed" description={errorMessage} />
          <SetupActions>
            <Button type="button" variant="outline" onClick={() => void runFinish()}>
              Retry
            </Button>
            <Button type="button" variant="outline" onClick={() => setCurrentScreen("validation")}>
              Back to validation
            </Button>
          </SetupActions>
        </>
      ) : null}

      {finished ? (
        <SetupActions>
          <Button type="button" onClick={handleDashboard}>
            Go to dashboard
          </Button>
          <Button type="button" variant="outline" onClick={() => void handleRestart()}>
            Restart setup
          </Button>
        </SetupActions>
      ) : !errorMessage ? (
        <SetupActions>
          <Button type="button" disabled={finishing} onClick={() => void runFinish()}>
            Complete setup
          </Button>
        </SetupActions>
      ) : null}
    </SetupForm>
  );
}
