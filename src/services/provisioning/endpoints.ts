/** Provisioning REST paths — /api/provisioning/* only */

export const provisioningEndpoints = {
  begin: "/api/provisioning/installation/begin",
  resume: "/api/provisioning/installation/resume",
  abort: "/api/provisioning/installation/abort",
  factoryReset: "/api/provisioning/installation/factory-reset",
  detectRouters: "/api/provisioning/routers/detect",
  selectDriver: "/api/provisioning/routers/select",
  connectRouter: "/api/provisioning/routers/connect",
  routerProfiles: "/api/provisioning/routers/profiles",
  configurePortal: "/api/provisioning/portal/configure",
  configureCoin: "/api/provisioning/coin/configure",
  validate: "/api/provisioning/validate",
  finish: "/api/provisioning/finish",
} as const;
