import { useMemo } from "react";
import {
  SetupForm,
  SetupFormField,
  SetupFormSection,
  SetupReadOnlyValue,
} from "@/components/setup/SetupForm";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { SetupStatusCard } from "@/components/setup/SetupStatusCard";
import { wizardTheme } from "@/components/setup/WizardTheme";
import type { SetupScreenProps } from "@/pages/setup/SetupScreenProps";
import { readValidationDraft } from "@/pages/setup/validationDraft";
import {
  buildInstallationReport,
  formatCoinSummaryLine,
  formatCompletedSteps,
  formatDriverSummaryLine,
  formatFirmwareSummaryLine,
  formatPortalSummaryLine,
  formatReportValue,
  formatRouterSummaryLine,
} from "@/models/InstallationReport";

export function SummaryScreen({ installation, session, workflowStep }: SetupScreenProps) {
  const validationDraft = readValidationDraft();

  const report = useMemo(
    () =>
      buildInstallationReport({
        installation,
        session,
        workflowStep,
        validation: validationDraft.results,
      }),
    [installation, session, validationDraft.results, workflowStep],
  );

  const { summary } = report;

  return (
    <SetupForm aria-label="Installation summary">
      <div>
        <h2 className={wizardTheme.typography.title}>Installation summary</h2>
        <p className={wizardTheme.typography.description}>
          Review configuration before marking this appliance production-ready.
        </p>
      </div>

      {!summary.validationPassed ? (
        <SetupInfoBanner
          variant="warning"
          title="Validation incomplete"
          description="Complete validation before finishing setup."
        />
      ) : null}

      {report.warnings.length > 0 ? (
        <SetupInfoBanner
          variant="warning"
          title="Warnings"
          description={report.warnings.map((item) => `${item.title}: ${item.description}`).join(" ")}
        />
      ) : null}

      <SetupStatusCard
        status={summary.validationPassed ? "success" : "warning"}
        title="Installation progress"
        description={`${summary.progressPercent}% complete`}
        details={formatCompletedSteps(summary)}
      />

      <SetupFormSection title="Installer">
        <SetupFormField label="Installer">
          <SetupReadOnlyValue value={formatReportValue(summary.installerName)} />
        </SetupFormField>
        <SetupFormField label="Elapsed time">
          <SetupReadOnlyValue value={formatReportValue(summary.elapsedTime)} />
        </SetupFormField>
      </SetupFormSection>

      <SetupFormSection title="Gateway">
        <SetupFormField label="Gateway">
          <SetupReadOnlyValue value={formatRouterSummaryLine(summary)} />
        </SetupFormField>
        <SetupFormField label="Hotspot profile">
          <SetupReadOnlyValue value={formatReportValue(summary.routerProfile)} />
        </SetupFormField>
        <SetupFormField label="Driver">
          <SetupReadOnlyValue value={formatDriverSummaryLine(summary)} />
        </SetupFormField>
        <SetupFormField label="Firmware">
          <SetupReadOnlyValue value={formatFirmwareSummaryLine(summary)} />
        </SetupFormField>
      </SetupFormSection>

      <SetupFormSection title="Portal">
        <SetupFormField label="Portal">
          <SetupReadOnlyValue value={formatPortalSummaryLine(summary)} />
        </SetupFormField>
      </SetupFormSection>

      <SetupFormSection title="Coin">
        <SetupFormField label="Coin">
          <SetupReadOnlyValue value={formatCoinSummaryLine(summary)} />
        </SetupFormField>
      </SetupFormSection>
    </SetupForm>
  );
}
