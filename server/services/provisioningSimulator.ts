/**
 * Dev simulator for /api/provisioning/* — mirrors firmware workflow shapes.
 */

type InstallationState =
  | "factory"
  | "router_selected"
  | "router_connected"
  | "portal_configured"
  | "coin_configured"
  | "validation_passed"
  | "ready";

type WorkflowStep =
  | "welcome"
  | "router_detection"
  | "driver_selection"
  | "router_connection"
  | "portal_configuration"
  | "coin_configuration"
  | "validation"
  | "summary"
  | "ready";

const STATE_ORDER: InstallationState[] = [
  "factory",
  "router_selected",
  "router_connected",
  "portal_configured",
  "coin_configured",
  "validation_passed",
  "ready",
];

const MIKROTIK_MANIFEST = {
  driverId: "mikrotik",
  vendor: "MikroTik",
  model: "RouterBOARD / CCR / hAP / wAP",
  supportedFirmware: "RouterOS",
  minimumVersion: "6.0",
  stability: "stable",
  documentationUrl: "https://help.mikrotik.com/",
  driverVersion: "1.0.0",
  capabilities: {
    supportsVoucherControl: true,
    supportsHotspot: true,
    supportsApi: true,
    supportsIdentity: true,
    supportsHealth: true,
    supportsStatistics: true,
  },
  supportedFeatures: [
    "hotspot_user_provisioning",
    "hotspot_session_disconnect",
    "profile_management",
    "router_identity",
  ],
};

function workflowStepForState(state: InstallationState): WorkflowStep {
  switch (state) {
    case "factory":
      return "welcome";
    case "router_selected":
      return "driver_selection";
    case "router_connected":
      return "router_connection";
    case "portal_configured":
      return "portal_configuration";
    case "coin_configured":
      return "coin_configuration";
    case "validation_passed":
      return "summary";
    case "ready":
      return "ready";
    default:
      return "welcome";
  }
}

function progressPercent(state: InstallationState): number {
  const idx = STATE_ORDER.indexOf(state);
  if (idx < 0) return 0;
  return Math.round((idx / (STATE_ORDER.length - 1)) * 100);
}

function nextState(state: InstallationState): InstallationState {
  const idx = STATE_ORDER.indexOf(state);
  return STATE_ORDER[Math.min(idx + 1, STATE_ORDER.length - 1)] ?? state;
}

function previousState(state: InstallationState): InstallationState {
  const idx = STATE_ORDER.indexOf(state);
  return STATE_ORDER[Math.max(idx - 1, 0)] ?? state;
}

type Session = {
  sessionId: string;
  startedAt: number;
  lastActivity: number;
  installerName: string;
  deviceId: string;
  isRecovery: boolean;
  attempt: number;
};

const store = {
  state: "factory" as InstallationState,
  session: null as Session | null,
  progressMessage: "",
  router: {
    host: "10.40.0.1",
    username: "admin",
    password: "",
    profile: "default",
    driverId: "mikrotik",
  },
  portal: {
    portalName: "Renz-Fi Piso WiFi",
    theme: "default",
  },
  coin: {
    enabled: true,
    pulsesPerPeso: 1,
    debounceMs: 35,
    settleMs: 450,
    timeoutSeconds: 60,
    defaultMinutesPerPeso: 5,
  },
  coinHardwareOk: true,
};

function buildInstallation() {
  const elapsedMs = store.session
    ? store.session.lastActivity - store.session.startedAt
    : 0;

  return {
    state: store.state,
    updatedAt: Date.now(),
    firmwareVersion: "dev-simulator",
    installationVersion: 2,
    progressPercent: progressPercent(store.state),
    stepIndex: STATE_ORDER.indexOf(store.state),
    stepCount: STATE_ORDER.length,
    needsSetup: store.state !== "ready",
    ready: store.state === "ready",
    nextState: nextState(store.state),
    previousState: previousState(store.state),
    completedSteps: [] as string[],
    session: store.session
      ? {
          ...store.session,
          elapsedMs,
          elapsedMinutes: Math.floor(elapsedMs / 60000),
        }
      : undefined,
  };
}

function workflowPayload(extra: Record<string, unknown> = {}) {
  return {
    ok: true,
    workflowStep: workflowStepForState(store.state),
    installation: buildInstallation(),
    ready: store.state === "ready",
    needsSetup: store.state !== "ready",
    ...extra,
  };
}

function touchSession() {
  if (store.session) {
    store.session.lastActivity = Date.now();
  }
}

function compareVersions(left: string, right: string): number {
  const lp = left.split(".").map(Number);
  const rp = right.split(".").map(Number);
  for (let i = 0; i < Math.max(lp.length, rp.length); i += 1) {
    const l = lp[i] ?? 0;
    const r = rp[i] ?? 0;
    if (l !== r) return l > r ? 1 : -1;
  }
  return 0;
}

export function resumeInstallation() {
  touchSession();
  const elapsedMinutes = store.session
    ? Math.floor((store.session.lastActivity - store.session.startedAt) / 60000)
    : 0;

  return workflowPayload({
    resumed: true,
    resumePrompt: Boolean(store.session),
    elapsedMinutes,
    sessionId: store.session?.sessionId,
  });
}

export function beginInstallation(body: { installerName?: string; isRecovery?: boolean } = {}) {
  if (store.state === "ready") {
    return workflowPayload({ started: true, alreadyReady: true });
  }

  if (!store.session) {
    store.session = {
      sessionId: `ins-dev-${Date.now()}`,
      startedAt: Date.now(),
      lastActivity: Date.now(),
      installerName: body.installerName ?? "",
      deviceId: "DE:AD:BE:EF:00:01",
      isRecovery: Boolean(body.isRecovery),
      attempt: 1,
    };
  } else {
    touchSession();
    if (body.installerName) store.session.installerName = body.installerName;
  }

  return workflowPayload({ started: true, alreadyReady: false });
}

export function abortInstallation() {
  touchSession();
  return workflowPayload({ aborted: true });
}

export function factoryResetInstallation() {
  store.state = "factory";
  store.session = null;
  store.progressMessage = "";
  store.portal = { portalName: "Renz-Fi Piso WiFi", theme: "default" };
  store.coin = {
    enabled: true,
    pulsesPerPeso: 1,
    debounceMs: 35,
    settleMs: 450,
    timeoutSeconds: 60,
    defaultMinutesPerPeso: 5,
  };
  store.coinHardwareOk = true;
  return workflowPayload({ reset: true });
}

export function detectRouters() {
  touchSession();
  return {
    ok: true,
    workflowStep: "router_detection" as WorkflowStep,
    available: [MIKROTIK_MANIFEST],
    drivers: [
      {
        driverId: "mikrotik",
        detected: false,
        configured: false,
        confidence: "none",
        reason: "RouterOS API live probe deferred to setup wizard",
        manifest: MIKROTIK_MANIFEST,
      },
    ],
    count: 1,
    installation: buildInstallation(),
  };
}

export function selectDriver(body: {
  driverId?: string;
  firmware?: string;
  version?: string;
  host?: string;
}) {
  touchSession();
  setProgressMessage("Selecting router driver");

  const driverId = body.driverId ?? "";
  if (!driverId) {
    return { ok: false, error: "driverId is required", installation: buildInstallation() };
  }

  const firmware = body.firmware ?? MIKROTIK_MANIFEST.supportedFirmware;
  const version = body.version ?? "";
  if (
    firmware &&
    firmware.toLowerCase() !== MIKROTIK_MANIFEST.supportedFirmware.toLowerCase()
  ) {
    return {
      ok: false,
      error: `Firmware '${firmware}' is not supported by the MikroTik driver`,
      reason: `Firmware '${firmware}' is not supported by the MikroTik driver`,
      installation: buildInstallation(),
    };
  }
  if (version && compareVersions(version, MIKROTIK_MANIFEST.minimumVersion) < 0) {
    const reason = `Firmware version ${version} is below the minimum supported ${MIKROTIK_MANIFEST.minimumVersion}`;
    return { ok: false, error: reason, reason, installation: buildInstallation() };
  }

  store.router.driverId = driverId;
  if (body.host) store.router.host = body.host;
  store.state = "router_selected";

  return workflowPayload({
    driverId,
    manifest: MIKROTIK_MANIFEST,
    supported: true,
  });
}

export function connectRouter(body: {
  host?: string;
  username?: string;
  password?: string;
  profile?: string;
}) {
  touchSession();
  setProgressMessage("Testing router connection");

  store.router.host = body.host ?? store.router.host;
  store.router.username = body.username ?? store.router.username;
  store.router.password = body.password ?? store.router.password;
  store.router.profile = body.profile ?? store.router.profile;

  if (!store.router.host || !store.router.username || !store.router.password) {
    return {
      ok: false,
      connected: false,
      error: "Router credentials are required",
      installation: buildInstallation(),
    };
  }

  store.state = "router_connected";

  return workflowPayload({
    connected: true,
    identity: "MikroTik-dev-simulator",
    profileFound: true,
  });
}

export function listRouterProfiles() {
  touchSession();
  if (store.state === "factory") {
    return { ok: false, profiles: [], error: "Select a driver first", installation: buildInstallation() };
  }
  return workflowPayload({
    profiles: ["default", "renzfi-guest"],
  });
}

export function configurePortal(body: Record<string, unknown> = {}) {
  touchSession();
  setProgressMessage("Applying portal configuration");

  if (store.state !== "router_connected" && !installationStateAtLeast(store.state, "router_connected")) {
    return {
      ok: false,
      error: "Complete router connection before configuring portal",
      installation: buildInstallation(),
    };
  }

  const portal = (body.portal as Record<string, unknown> | undefined) ?? body;
  if (portal.portalName) store.portal.portalName = String(portal.portalName);
  if (portal.theme) store.portal.theme = String(portal.theme);

  store.state = "portal_configured";

  return workflowPayload({
    verified: true,
    revision: 1,
    hasBanner: false,
    hasMusic: false,
  });
}

function installationStateAtLeast(current: InstallationState, required: InstallationState): boolean {
  return STATE_ORDER.indexOf(current) >= STATE_ORDER.indexOf(required);
}

export function configureCoin(body: Record<string, unknown> = {}) {
  touchSession();
  setProgressMessage("Configuring coin acceptor");

  if (!installationStateAtLeast(store.state, "portal_configured")) {
    return {
      ok: false,
      error: "Configure portal before coin acceptor",
      installation: buildInstallation(),
    };
  }

  const coinBody = (body.coin as Record<string, unknown> | undefined) ?? body;
  if (coinBody.enabled != null) store.coin.enabled = Boolean(coinBody.enabled);
  if (coinBody.pulsesPerPeso != null) store.coin.pulsesPerPeso = Number(coinBody.pulsesPerPeso);
  if (coinBody.debounceMs != null) store.coin.debounceMs = Number(coinBody.debounceMs);
  if (coinBody.settleMs != null) store.coin.settleMs = Number(coinBody.settleMs);
  if (coinBody.timeoutSeconds != null) {
    store.coin.timeoutSeconds = Number(coinBody.timeoutSeconds);
  }
  if (coinBody.defaultMinutesPerPeso != null) {
    store.coin.defaultMinutesPerPeso = Number(coinBody.defaultMinutesPerPeso);
  }

  const simulateFault = coinBody.pulsesPerPeso === 0;
  store.coinHardwareOk = !simulateFault;

  if (simulateFault) {
    return {
      ok: false,
      hardwareOk: false,
      hardware: {
        enabled: store.coin.enabled,
        fault: true,
        pulseCount: 0,
        lastPulseMs: 0,
      },
      error: "Coin hardware fault detected",
      installation: buildInstallation(),
    };
  }

  store.state = "coin_configured";

  return workflowPayload({
    hardwareOk: store.coinHardwareOk,
    hardware: {
      enabled: store.coin.enabled,
      fault: false,
      pulseCount: 12,
      lastPulseMs: 34,
      debounceMs: store.coin.debounceMs,
      settleMs: store.coin.settleMs,
      timeoutSeconds: store.coin.timeoutSeconds,
      pulsesPerPeso: store.coin.pulsesPerPeso,
    },
  });
}

function buildChecks() {
  const routerReady = installationStateAtLeast(store.state, "router_connected");
  const portalReady = installationStateAtLeast(store.state, "portal_configured");
  const coinReady =
    installationStateAtLeast(store.state, "coin_configured") && store.coinHardwareOk !== false;
  const storageOk = true;
  const assetsOk = portalReady;
  const networkOk = routerReady && Boolean(store.router.host);
  const firmwareOk = true;

  return [
    {
      id: "router_connected",
      passed: routerReady,
      detail: routerReady ? "Router connection verified" : "Router connection step incomplete",
    },
    {
      id: "portal_configured",
      passed: portalReady,
      detail: portalReady ? "Portal configuration verified" : "Portal configuration unavailable",
    },
    {
      id: "coin_ready",
      passed: coinReady,
      detail: coinReady ? "Coin hardware ready" : "Coin hardware not ready",
      severity: coinReady ? "info" : "error",
    },
    {
      id: "storage_healthy",
      passed: storageOk,
      detail: storageOk ? "Storage available" : "Storage degraded or unavailable",
    },
    {
      id: "assets_ready",
      passed: assetsOk,
      detail: assetsOk
        ? "Bundled portal assets verified"
        : "Portal assets not verified",
      severity: assetsOk ? "info" : "warning",
    },
    {
      id: "network_ready",
      passed: networkOk,
      detail: networkOk ? "Router network reachable" : "Router network not verified",
    },
    {
      id: "firmware_ready",
      passed: firmwareOk,
      detail: "Appliance firmware dev-simulator",
      severity: "info" as const,
    },
  ];
}

export function validateInstallation() {
  touchSession();
  setProgressMessage("Running installation checks");

  if (!installationStateAtLeast(store.state, "coin_configured")) {
    return workflowPayload({
      ok: false,
      passed: false,
      error: "Complete coin configuration before validation",
      checks: buildChecks(),
    });
  }

  const checks = buildChecks();
  const allPassed = checks.every((check) => check.passed);

  if (allPassed) {
    store.state = "validation_passed";
  }

  return workflowPayload({
    ok: allPassed,
    passed: allPassed,
    checks,
    error: allPassed ? undefined : "One or more installation checks failed",
  });
}

export function finalizeInstallation(
  eventBus?: { publishRaw: (name: string, payload: unknown) => void },
) {
  touchSession();
  setProgressMessage("Finalizing installation");

  if (!installationStateAtLeast(store.state, "validation_passed")) {
    return workflowPayload({
      ok: false,
      finished: false,
      error: "Validation must pass before finishing installation",
    });
  }

  store.state = "ready";

  const summary = {
    router: {
      host: store.router.host,
      username: store.router.username,
      profile: store.router.profile,
      driverId: store.router.driverId,
    },
    portal: {
      portalName: store.portal.portalName,
      theme: store.portal.theme,
      revision: 1,
      hasBanner: false,
      hasMusic: false,
    },
    coin: { ...store.coin, hardwareOk: store.coinHardwareOk },
    firmwareVersion: "dev-simulator",
  };

  const payload = workflowPayload({
    finished: true,
    summary,
  });

  eventBus?.publishRaw("installation.completed", {
    summary,
    installation: payload.installation,
    sessionId: store.session?.sessionId,
  });

  return payload;
}

export function emitInstallationProgress(eventBus: { publishRaw: (name: string, payload: unknown) => void }) {
  const payload = {
    step: workflowStepForState(store.state),
    percent: progressPercent(store.state),
    message: store.progressMessage || "Simulator progress update",
    sessionId: store.session?.sessionId,
  };
  eventBus.publishRaw("installation.progress", payload);
}

export function setProgressMessage(message: string) {
  store.progressMessage = message;
}
