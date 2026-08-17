import { api } from "@/services/api";
import type {
  AbortInstallationResponse,
  BeginInstallationBody,
  BeginInstallationResponse,
  FactoryResetResponse,
  ResumeResponse,
} from "@/types/provisioning";
import type {
  ConfigureCoinResponse,
  ConfigurePortalResponse,
  ConnectRouterBody,
  ConnectRouterResponse,
  DetectRoutersResponse,
  FinishInstallationResponse,
  RouterProfilesResponse,
  SelectDriverBody,
  SelectDriverResponse,
  ValidateInstallationResponse,
} from "@/types/routerProvisioning";
import type { CoinApplianceConfig, PortalApplianceConfig } from "@/lib/applianceConfiguration";
import {
  coinToProvisioningBody,
  portalToProvisioningBody,
} from "@/lib/applianceConfiguration";
import { provisioningEndpoints } from "./endpoints";

/** Typed wrapper for /api/provisioning/* — pages must not call fetch directly. */
export const provisioningClient = {
  resume: () => api.get<ResumeResponse>(provisioningEndpoints.resume),

  begin: (body?: BeginInstallationBody) =>
    api.post<BeginInstallationResponse>(provisioningEndpoints.begin, body),

  abort: () => api.post<AbortInstallationResponse>(provisioningEndpoints.abort),

  factoryReset: () => api.post<FactoryResetResponse>(provisioningEndpoints.factoryReset),

  detectRouters: () => api.get<DetectRoutersResponse>(provisioningEndpoints.detectRouters),

  selectDriver: (body: SelectDriverBody) =>
    api.post<SelectDriverResponse>(provisioningEndpoints.selectDriver, body),

  connectRouter: (body: ConnectRouterBody) =>
    api.post<ConnectRouterResponse>(provisioningEndpoints.connectRouter, body),

  listRouterProfiles: () =>
    api.get<RouterProfilesResponse>(provisioningEndpoints.routerProfiles),

  configurePortal: (portal: PortalApplianceConfig) =>
    api.post<ConfigurePortalResponse>(
      provisioningEndpoints.configurePortal,
      portalToProvisioningBody(portal),
    ),

  configureCoin: (coin: CoinApplianceConfig) =>
    api.post<ConfigureCoinResponse>(
      provisioningEndpoints.configureCoin,
      coinToProvisioningBody(coin),
    ),

  configurePortalRaw: (body?: Record<string, unknown>) =>
    api.post<ConfigurePortalResponse>(provisioningEndpoints.configurePortal, body),

  configureCoinRaw: (body?: Record<string, unknown>) =>
    api.post<ConfigureCoinResponse>(provisioningEndpoints.configureCoin, body),

  validate: () => api.post<ValidateInstallationResponse>(provisioningEndpoints.validate),

  finish: () => api.post<FinishInstallationResponse>(provisioningEndpoints.finish),
};

export type ProvisioningClient = typeof provisioningClient;
