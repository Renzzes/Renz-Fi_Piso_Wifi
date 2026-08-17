import { Router } from "express";
import { sendError, sendSuccess } from "../utils/response.js";
import { eventBus } from "../services/eventBus.js";
import {
  configureCoin,
  configurePortal,
  finalizeInstallation,
  validateInstallation,
  abortInstallation,
  beginInstallation,
  connectRouter,
  detectRouters,
  factoryResetInstallation,
  listRouterProfiles,
  resumeInstallation,
  selectDriver,
  emitInstallationProgress,
  setProgressMessage,
} from "../services/provisioningSimulator.js";

export const provisioningRouter = Router();

provisioningRouter.get("/installation/resume", (_req, res) => {
  setProgressMessage("Resuming installation session");
  const data = resumeInstallation();
  emitInstallationProgress(eventBus);
  sendSuccess(res, data);
});

provisioningRouter.post("/installation/begin", (req, res) => {
  setProgressMessage("Installation workflow started");
  const data = beginInstallation(req.body ?? {});
  emitInstallationProgress(eventBus);
  sendSuccess(res, data);
});

provisioningRouter.post("/installation/abort", (_req, res) => {
  const data = abortInstallation();
  eventBus.publishRaw("installation.aborted", { state: data.installation.state });
  sendSuccess(res, data);
});

provisioningRouter.post("/installation/factory-reset", (_req, res) => {
  const data = factoryResetInstallation();
  sendSuccess(res, data);
});

provisioningRouter.get("/routers/detect", (_req, res) => {
  setProgressMessage("Scanning for router drivers");
  const data = detectRouters();
  emitInstallationProgress(eventBus);
  sendSuccess(res, data);
});

provisioningRouter.post("/routers/select", (req, res) => {
  setProgressMessage("Selecting router driver");
  const data = selectDriver(req.body ?? {});
  emitInstallationProgress(eventBus);
  if (data.ok === false) {
    return sendError(res, {
      error: String(data.error ?? "Unable to select driver"),
      code: "PROVISIONING_ERROR",
      status: 400,
    });
  }
  sendSuccess(res, data);
});

provisioningRouter.post("/routers/connect", (req, res) => {
  setProgressMessage("Testing router connection");
  const data = connectRouter(req.body ?? {});
  emitInstallationProgress(eventBus);
  if (data.ok === false || data.connected === false) {
    return sendError(res, {
      error: String(data.error ?? "Router connection failed"),
      code: "PROVISIONING_ERROR",
      status: 400,
    });
  }
  sendSuccess(res, data);
});

provisioningRouter.get("/routers/profiles", (_req, res) => {
  const data = listRouterProfiles();
  if (data.ok === false) {
    return sendSuccess(res, data);
  }
  sendSuccess(res, data);
});

provisioningRouter.post("/portal/configure", (req, res) => {
  setProgressMessage("Applying portal configuration");
  const data = configurePortal(req.body ?? {});
  emitInstallationProgress(eventBus);
  if (data.ok === false || data.verified !== true) {
    return sendError(res, {
      error: String(data.error ?? "Portal branding verification failed"),
      code: "PROVISIONING_ERROR",
      status: 400,
    });
  }
  sendSuccess(res, data);
});

provisioningRouter.post("/coin/configure", (req, res) => {
  setProgressMessage("Configuring coin acceptor");
  const data = configureCoin(req.body ?? {});
  emitInstallationProgress(eventBus);
  if (data.ok === false && !data.skipped) {
    return sendError(res, {
      error: String(data.error ?? "Coin configuration failed"),
      code: "PROVISIONING_ERROR",
      status: 400,
    });
  }
  sendSuccess(res, data);
});

provisioningRouter.post("/validate", (_req, res) => {
  setProgressMessage("Running installation checks");
  const data = validateInstallation();
  emitInstallationProgress(eventBus);
  sendSuccess(res, data, data.passed ? "Validation passed" : "Validation failed");
});

provisioningRouter.post("/finish", (_req, res) => {
  setProgressMessage("Finalizing installation");
  const data = finalizeInstallation(eventBus);
  emitInstallationProgress(eventBus);
  if (data.ok === false || data.finished !== true) {
    return sendError(res, {
      error: String(data.error ?? "Unable to finish installation"),
      code: "PROVISIONING_ERROR",
      status: 400,
    });
  }
  sendSuccess(res, data, "Installation complete");
});
