import type { InstallationState, WorkflowStep } from "@/types/provisioning";

/** Ordered setup screens — single source for navigation guards. */
export const SETUP_SCREEN_ORDER = [
  "welcome",
  "router_detection",
  "driver_selection",
  "router_connection",
  "portal_configuration",
  "coin_configuration",
  "validation",
  "summary",
  "complete",
] as const;

export type SetupScreenId = (typeof SETUP_SCREEN_ORDER)[number];

/** Frozen mapping: workflowStep → screen (never use if/switch chains in pages). */
export const WORKFLOW_STEP_TO_SCREEN: Record<WorkflowStep, SetupScreenId> = {
  welcome: "welcome",
  router_detection: "router_detection",
  driver_selection: "driver_selection",
  router_connection: "router_connection",
  portal_configuration: "portal_configuration",
  coin_configuration: "coin_configuration",
  validation: "validation",
  summary: "summary",
  ready: "complete",
};

/** Human-readable labels for WizardShell step indicator. */
export const SETUP_SCREEN_LABELS: Record<SetupScreenId, string> = {
  welcome: "Welcome",
  // Phase 6C: installer-friendly labels — no driver/manifest terminology exposed.
  router_detection: "Network Type",
  driver_selection: "Network Type",
  router_connection: "Router Setup",
  portal_configuration: "Portal",
  coin_configuration: "Coin",
  validation: "Validation",
  summary: "Summary",
  complete: "Complete",
};

/** Max screen index allowed from persisted installation state. */
const STATE_MAX_SCREEN: Record<InstallationState, SetupScreenId> = {
  factory: "router_detection",
  router_selected: "driver_selection",
  router_connected: "router_connection",
  portal_configured: "portal_configuration",
  coin_configured: "coin_configuration",
  validation_passed: "summary",
  ready: "complete",
};

export function screenIndex(id: SetupScreenId): number {
  return SETUP_SCREEN_ORDER.indexOf(id);
}

export function screenFromWorkflowStep(step: WorkflowStep): SetupScreenId {
  return WORKFLOW_STEP_TO_SCREEN[step];
}

export function workflowStepFromScreen(screen: SetupScreenId): WorkflowStep {
  if (screen === "complete") return "ready";
  return screen as WorkflowStep;
}

export function maxAllowedScreen(state: InstallationState): SetupScreenId {
  return STATE_MAX_SCREEN[state] ?? "welcome";
}

export function maxAllowedScreenIndex(state: InstallationState): number {
  return screenIndex(maxAllowedScreen(state));
}

/** Target screen when resuming an in-progress installation. */
export function resumeScreenForState(state: InstallationState): SetupScreenId {
  const targets: Partial<Record<InstallationState, SetupScreenId>> = {
    factory: "router_detection",
    router_selected: "router_connection",
    router_connected: "portal_configuration",
    portal_configured: "coin_configuration",
    coin_configured: "validation",
    validation_passed: "summary",
    ready: "complete",
  };
  return targets[state] ?? "welcome";
}

/** Screen to open after a workflow step completes successfully. */
export function nextScreenAfterState(state: InstallationState): SetupScreenId {
  return resumeScreenForState(state);
}
