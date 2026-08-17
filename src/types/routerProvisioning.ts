import type { WorkflowResponse } from "@/types/provisioning";

export type DriverStability = "stable" | "experimental";

export type RouterCapabilities = {
  supportsVoucherControl?: boolean;
  supportsBandwidthLimit?: boolean;
  supportsHotspot?: boolean;
  supportsApi?: boolean;
  supportsIdentity?: boolean;
  supportsHealth?: boolean;
  supportsStatistics?: boolean;
  supportsRemoteConfig?: boolean;
};

export type RouterDriverManifest = {
  driverId: string;
  vendor: string;
  model?: string;
  supportedFirmware?: string;
  minimumVersion?: string;
  stability?: DriverStability;
  documentationUrl?: string;
  driverVersion?: string;
  capabilities?: RouterCapabilities;
  supportedFeatures?: string[];
};

export type DriverDetectionEntry = {
  driverId: string;
  detected?: boolean;
  configured?: boolean;
  confidence?: string | number;
  reason?: string;
  host?: string;
  active?: boolean;
  manifest?: RouterDriverManifest;
};

export type DetectRoutersResponse = WorkflowResponse & {
  available?: RouterDriverManifest[];
  drivers?: DriverDetectionEntry[];
  count?: number;
};

export type SelectDriverBody = {
  driverId: string;
  firmware?: string;
  version?: string;
  host?: string;
  username?: string;
  password?: string;
  profile?: string;
};

export type SelectDriverResponse = WorkflowResponse & {
  driverId?: string;
  manifest?: RouterDriverManifest;
  supported?: boolean;
  skipped?: boolean;
  reason?: string;
};

export type ConnectRouterBody = {
  host: string;
  username: string;
  password: string;
  profile?: string;
};

export type ConnectRouterResponse = WorkflowResponse & {
  connected?: boolean;
  identity?: string;
  profileFound?: boolean;
  error?: string;
};

export type ConfigurePortalResponse = WorkflowResponse & {
  verified?: boolean;
  revision?: number;
  hasBanner?: boolean;
  hasMusic?: boolean;
};

export type ConfigureCoinResponse = WorkflowResponse & {
  hardwareOk?: boolean;
  skipped?: boolean;
  reason?: string;
  hardware?: Record<string, unknown>;
};

export type ProvisioningCheck = {
  id: string;
  passed: boolean;
  detail?: string;
  severity?: "info" | "warning" | "error";
};

export type ValidateInstallationResponse = WorkflowResponse & {
  passed?: boolean;
  checks?: ProvisioningCheck[];
};

export type FinishInstallationSummary = {
  router?: Record<string, unknown>;
  portal?: Record<string, unknown>;
  coin?: Record<string, unknown>;
  firmwareVersion?: string;
};

export type FinishInstallationResponse = WorkflowResponse & {
  finished?: boolean;
  summary?: FinishInstallationSummary;
  reason?: string;
};

export type RouterProfilesResponse = WorkflowResponse & {
  profiles?: string[];
};
