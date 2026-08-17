/** Frozen contract types — see ESP32_S3_Firmware/docs/PROVISIONING_API.md */

export const INSTALLATION_STATES = [
  "factory",
  "router_selected",
  "router_connected",
  "portal_configured",
  "coin_configured",
  "validation_passed",
  "ready",
] as const;

export type InstallationState = (typeof INSTALLATION_STATES)[number];

export const WORKFLOW_STEPS = [
  "welcome",
  "router_detection",
  "driver_selection",
  "router_connection",
  "portal_configuration",
  "coin_configuration",
  "validation",
  "summary",
  "ready",
] as const;

export type WorkflowStep = (typeof WORKFLOW_STEPS)[number];

export type InstallationSession = {
  sessionId?: string;
  startedAt?: number;
  lastActivity?: number;
  installerName?: string;
  deviceId?: string;
  resumeToken?: string;
  isRecovery?: boolean;
  attempt?: number;
  elapsedMs?: number;
  elapsedMinutes?: number;
};

export type InstallationStatus = {
  state: InstallationState;
  updatedAt?: number;
  firmwareVersion?: string;
  installationVersion?: number;
  progressPercent: number;
  stepIndex?: number;
  stepCount?: number;
  needsSetup?: boolean;
  ready?: boolean;
  nextState?: InstallationState;
  previousState?: InstallationState;
  completedSteps?: string[];
  session?: InstallationSession;
};

export type ProvisioningProgress = {
  step: string;
  percent: number;
  message: string;
  sessionId?: string;
};

export type WorkflowResponse = {
  ok?: boolean;
  workflowStep: WorkflowStep;
  installation: InstallationStatus;
  ready?: boolean;
  needsSetup?: boolean;
  error?: string;
};

export type ResumeResponse = WorkflowResponse & {
  resumed?: boolean;
  resumePrompt?: boolean;
  elapsedMinutes?: number;
  sessionId?: string;
};

export type BeginInstallationBody = {
  installerName?: string;
  isRecovery?: boolean;
};

export type BeginInstallationResponse = WorkflowResponse & {
  started?: boolean;
  alreadyReady?: boolean;
};

export type AbortInstallationResponse = WorkflowResponse & {
  aborted?: boolean;
};

export type FactoryResetResponse = WorkflowResponse & {
  reset?: boolean;
};

export type InstallationStateChangedEvent = {
  state: InstallationState;
  nextState?: InstallationState;
  progressPercent?: number;
  sessionId?: string;
};

export type InstallationCompletedEvent = {
  summary?: Record<string, unknown>;
};

export type InstallationAbortedEvent = {
  state?: InstallationState;
};
