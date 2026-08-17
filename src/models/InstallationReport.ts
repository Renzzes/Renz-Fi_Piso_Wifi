import type { SetupSummaryModel } from "@/pages/setup/SetupSummaryModel";
import { buildSetupSummaryModel } from "@/pages/setup/SetupSummaryModel";
import type { BuildSetupSummaryModelInput } from "@/pages/setup/SetupSummaryModel";
import {
  partitionValidationResults,
  type ValidationResult,
} from "@/models/ValidationResult";

/** Canonical installation report — PDF export, support bundle, factory report, cloud onboarding. */
export interface InstallationReport {
  summary: SetupSummaryModel;
  validation: ValidationResult[];
  warnings: ValidationResult[];
  recommendations: ValidationResult[];
  generatedAt: string;
}

export type BuildInstallationReportInput = BuildSetupSummaryModelInput & {
  validation?: ValidationResult[];
};

/** Assemble report from SetupSummaryModel + validation results only. */
export function buildInstallationReport({
  validation = [],
  ...summaryInput
}: BuildInstallationReportInput): InstallationReport {
  const summary = buildSetupSummaryModel(summaryInput);
  const { warnings, recommendations } = partitionValidationResults(validation);

  return {
    summary,
    validation,
    warnings,
    recommendations,
    generatedAt: new Date().toISOString(),
  };
}

/** Display helpers — formatting only, no summary field assembly. */
export function formatReportValue(
  value: string | number | boolean | null | undefined,
): string {
  if (value == null || value === "") return "—";
  if (typeof value === "boolean") return value ? "Yes" : "No";
  return String(value);
}

export function formatRouterSummaryLine(model: SetupSummaryModel): string {
  const parts = [
    model.routerVendor,
    model.routerModel,
    model.routerHost ? `@ ${model.routerHost}` : null,
  ].filter(Boolean);
  return parts.length > 0 ? parts.join(" · ") : "—";
}

export function formatDriverSummaryLine(model: SetupSummaryModel): string {
  const parts = [model.driver, model.driverVersion, model.driverStability].filter(Boolean);
  return parts.length > 0 ? parts.join(" · ") : "—";
}

export function formatFirmwareSummaryLine(model: SetupSummaryModel): string {
  const parts = [model.applianceFirmwareVersion, model.firmwareVersion].filter(Boolean);
  return parts.length > 0 ? parts.join(" · ") : "—";
}

export function formatPortalSummaryLine(model: SetupSummaryModel): string {
  const parts = [model.portalName, model.theme ? `theme: ${model.theme}` : null].filter(Boolean);
  return parts.length > 0 ? parts.join(" · ") : "—";
}

export function formatCoinSummaryLine(model: SetupSummaryModel): string {
  if (model.coinEnabled == null && !model.pricingProfile) return "—";
  const enabled = model.coinEnabled == null ? null : model.coinEnabled ? "enabled" : "disabled";
  const parts = [enabled, model.pricingProfile].filter(Boolean);
  return parts.length > 0 ? parts.join(" · ") : "—";
}

export function formatCompletedSteps(model: SetupSummaryModel): string {
  if (!model.completedSteps.length) return "—";
  return model.completedSteps.join(", ");
}
