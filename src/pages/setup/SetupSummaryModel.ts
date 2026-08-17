/**
 * Frozen setup summary model — single read model for Validation, Summary, Complete,
 * future PDF exports, diagnostics, and support logs.
 *
 * Build via `buildSetupSummaryModel()` only. Screens must not reassemble these fields.
 */
import type {
  InstallationSession,
  InstallationState,
  InstallationStatus,
  WorkflowStep,
} from "@/types/provisioning";
import { readRouterDraft } from "@/pages/setup/routerDraft";
import { readApplianceConfigDraft } from "@/pages/setup/applianceConfigDraft";

const STATE_ORDER: InstallationState[] = [
  "factory",
  "router_selected",
  "router_connected",
  "portal_configured",
  "coin_configured",
  "validation_passed",
  "ready",
];

function installationStateAtLeast(
  current: InstallationState,
  required: InstallationState,
): boolean {
  return STATE_ORDER.indexOf(current) >= STATE_ORDER.indexOf(required);
}

function formatElapsedTime(minutes: number | null, elapsedMs?: number): string | null {
  if (minutes != null && minutes >= 0) {
    return minutes === 1 ? "1 minute" : `${minutes} minutes`;
  }
  if (elapsedMs != null && elapsedMs >= 0) {
    const mins = Math.floor(elapsedMs / 60000);
    if (mins <= 0) return "Less than 1 minute";
    return mins === 1 ? "1 minute" : `${mins} minutes`;
  }
  return null;
}

/** Canonical setup snapshot — shared across summary-style screens and exports. */
export interface SetupSummaryModel {
  /** Installer */
  installerName: string | null;

  /** Appliance */
  sessionId: string | null;
  deviceId: string | null;
  applianceFirmwareVersion: string | null;

  /** Router driver / hardware */
  routerVendor: string | null;
  routerModel: string | null;
  routerHost: string | null;
  routerUsername: string | null;
  routerProfile: string | null;
  driverId: string | null;
  driver: string | null;
  driverVersion: string | null;
  driverStability: string | null;

  /** Router firmware (RouterOS version entered during setup) */
  firmware: string | null;
  firmwareVersion: string | null;

  /** Step completion flags (derived from persisted installation state) */
  routerConfigured: boolean;
  portalConfigured: boolean;
  coinConfigured: boolean;
  validationPassed: boolean;
  ready: boolean;

  /** Portal / coin snapshot (from appliance config draft) */
  portalName: string | null;
  theme: string | null;
  coinEnabled: boolean | null;
  pricingProfile: string | null;

  /** Timing */
  elapsedTimeMinutes: number | null;
  elapsedTime: string | null;

  /** Workflow meta */
  installationState: InstallationState;
  workflowStep: WorkflowStep | null;
  progressPercent: number;
  completedSteps: string[];
}

export type BuildSetupSummaryModelInput = {
  installation: InstallationStatus;
  session?: InstallationSession;
  workflowStep?: WorkflowStep | null;
};

/** Assemble summary from installation context + router draft (sessionStorage). */
export function buildSetupSummaryModel({
  installation,
  session,
  workflowStep = null,
}: BuildSetupSummaryModelInput): SetupSummaryModel {
  const draft = readRouterDraft();
  const appliance = readApplianceConfigDraft();
  const mergedSession = session ?? installation.session;
  const manifest = draft.selectedManifest;

  const elapsedMinutes = mergedSession?.elapsedMinutes ?? null;
  const elapsedMs = mergedSession?.elapsedMs;

  return {
    installerName: mergedSession?.installerName?.trim() || null,

    sessionId: mergedSession?.sessionId ?? null,
    deviceId: mergedSession?.deviceId ?? null,
    applianceFirmwareVersion: installation.firmwareVersion ?? null,

    routerVendor: manifest?.vendor ?? null,
    routerModel: manifest?.model ?? null,
    routerHost: draft.host?.trim() || null,
    routerUsername: draft.username?.trim() || null,
    routerProfile: draft.profile?.trim() || null,
    driverId: draft.selectedDriverId ?? manifest?.driverId ?? null,
    driver: manifest?.vendor ?? draft.selectedDriverId ?? null,
    driverVersion: manifest?.driverVersion ?? null,
    driverStability: manifest?.stability ?? null,

    firmware: draft.firmware?.trim() || manifest?.supportedFirmware || null,
    firmwareVersion: draft.version?.trim() || null,

    routerConfigured: installationStateAtLeast(installation.state, "router_connected"),
    portalConfigured: installationStateAtLeast(installation.state, "portal_configured"),
    coinConfigured: installationStateAtLeast(installation.state, "coin_configured"),
    validationPassed: installationStateAtLeast(installation.state, "validation_passed"),
    ready: installation.ready === true || installation.state === "ready",

    portalName: appliance.portal.portalName?.trim() || null,
    theme: appliance.portal.theme || null,
    coinEnabled: appliance.coin.enabled ?? null,
    pricingProfile: appliance.coin.pricingProfile?.trim() || null,

    elapsedTimeMinutes: elapsedMinutes,
    elapsedTime: formatElapsedTime(elapsedMinutes, elapsedMs),

    installationState: installation.state,
    workflowStep,
    progressPercent: installation.progressPercent ?? 0,
    completedSteps: installation.completedSteps ?? [],
  };
}

/** JSON-safe record for support logs, PDF, or clipboard export. */
export function serializeSetupSummaryModel(
  model: SetupSummaryModel,
): Record<string, string | boolean | number | null | string[]> {
  return {
    installerName: model.installerName,
    sessionId: model.sessionId,
    deviceId: model.deviceId,
    applianceFirmwareVersion: model.applianceFirmwareVersion,
    routerVendor: model.routerVendor,
    routerModel: model.routerModel,
    routerHost: model.routerHost,
    routerUsername: model.routerUsername,
    routerProfile: model.routerProfile,
    driverId: model.driverId,
    driver: model.driver,
    driverVersion: model.driverVersion,
    driverStability: model.driverStability,
    firmware: model.firmware,
    firmwareVersion: model.firmwareVersion,
    routerConfigured: model.routerConfigured,
    portalConfigured: model.portalConfigured,
    coinConfigured: model.coinConfigured,
    validationPassed: model.validationPassed,
    ready: model.ready,
    portalName: model.portalName,
    theme: model.theme,
    coinEnabled: model.coinEnabled,
    pricingProfile: model.pricingProfile,
    elapsedTimeMinutes: model.elapsedTimeMinutes,
    elapsedTime: model.elapsedTime,
    installationState: model.installationState,
    workflowStep: model.workflowStep,
    progressPercent: model.progressPercent,
    completedSteps: model.completedSteps,
  };
}
