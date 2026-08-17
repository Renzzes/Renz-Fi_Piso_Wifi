import { Router } from "express";
import { db } from "../db/connection.js";
import { getSystemStatus } from "../services/systemStatus.js";
import { createTimestampedBackup, runIntegrityCheck } from "../services/dbHealth.js";
import { logStructured } from "../services/logger.js";
import { publishAdminEvent } from "../services/eventBus.js";
import { sendSuccess } from "../utils/response.js";

export const systemRouter = Router();

const MAINTENANCE_TIMEOUT_SECONDS = 600;

type SimManagementApState = {
  running: boolean;
  enabled: boolean;
  mode: "factory" | "maintenance" | "disabled";
  keepEnabledAfterSetup: boolean;
  startedAtMs: number | null;
  clients: number;
};

const simManagementAp: SimManagementApState = {
  running: true,
  enabled: true,
  mode: "factory",
  keepEnabledAfterSetup: false,
  startedAtMs: Date.now(),
  clients: 0,
};

function buildSimManagementAp() {
  const uptimeSeconds =
    simManagementAp.running && simManagementAp.startedAtMs != null
      ? Math.floor((Date.now() - simManagementAp.startedAtMs) / 1000)
      : null;
  return {
    ssid: "RenzFi-Setup-RF-DEV001",
    ip: "192.168.4.1",
    running: simManagementAp.running,
    enabled: simManagementAp.enabled,
    mode: simManagementAp.mode,
    clients: simManagementAp.clients,
    connectedClients: simManagementAp.clients,
    uptimeSeconds,
    timeoutSeconds:
      simManagementAp.mode === "maintenance" ? MAINTENANCE_TIMEOUT_SECONDS : null,
    portalUrl: "http://192.168.4.1",
    security: "open",
  };
}

function buildSimNetworkPayload() {
  const managementAp = buildSimManagementAp();
  const ethernet = {
    link: true,
    linkUp: true,
    ip: "10.40.0.2",
    gateway: "10.40.0.1",
    subnet: "255.255.255.0",
    dns: "10.40.0.1",
    mac: "02:AA:BB:10:40:02",
  };
  return {
    interfaces: { managementAp, ethernet },
    managementAp,
    ethernet,
    mode: "ethernet",
    modeLabel: "W5500 wired (link up)",
    mdns: {
      hostname: "renzfi.local",
      adminUrl: "http://renzfi.local/admin",
    },
  };
}

const factoryResetTables = [
  "promo_rates",
  "vouchers",
  "sales_transactions",
  "active_sessions",
  "logs",
  "portal_settings",
  "coin_settings",
  "router_settings",
  "sync_queue",
  "admin_sessions",
] as const;

systemRouter.get("/coin", (_req, res) => {
  const status = getSystemStatus();
  const coin = status.coinSlot;
  return sendSuccess(res, {
    enabled: coin.enabled ?? false,
    state: coin.hardwareState ?? coin.state ?? "DISABLED",
    totalPulseCount: coin.totalPulseCount ?? 0,
    totalCoinCount: coin.totalCoinCount ?? 0,
    uptimePulseCount: coin.uptimePulseCount ?? 0,
    uptimeCoinCount: coin.uptimeCoinCount ?? 0,
    lastPulseTimestamp: coin.lastPulseTimestamp ?? null,
    lastCoinTimestamp: coin.lastCoinTimestamp ?? null,
  });
});

systemRouter.get("/rgb", (_req, res) => {
  return sendSuccess(res, {
    enabled: true,
    brightness: 80,
    mode: "SYSTEM_STATUS",
    state: "IDLE",
    colorName: "BLUE",
    color: { red: 0, green: 0, blue: 255 },
  });
});

systemRouter.put("/rgb", (req, res) => {
  publishAdminEvent("rgb.changed");
  return sendSuccess(res, {
    enabled: req.body?.enabled ?? true,
    brightness: req.body?.brightness ?? 80,
    mode: "SYSTEM_STATUS",
    state: "IDLE",
    colorName: "BLUE",
    color: { red: 0, green: 0, blue: 255 },
  });
});

systemRouter.get("/health", (_req, res) => {
  const status = getSystemStatus();
  return sendSuccess(res, {
    level: "HEALTHY",
    ethernet: { driver: "UP", link: "UP", ip: "127.0.0.1" },
    storage: status.storageStatus,
    coin: status.coinSlot,
    rgb: { mode: "SYSTEM_STATUS", brightness: 80, colorName: "BLUE", color: { red: 0, green: 0, blue: 255 } },
    memory: { heap: 180000, minimumHeap: 120000 },
  });
});

systemRouter.post("/reboot", (_req, res) => {
  logStructured({ level: "WARN", category: "system", message: "System reboot requested" });
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok: true }, "Reboot scheduled (simulator only)");
});

systemRouter.post("/factory-reset", (_req, res) => {
  logStructured({ level: "INFO", category: "system", message: "factory reset started" });
  db.transaction(() => {
    for (const table of factoryResetTables) {
      db.prepare(`DELETE FROM ${table}`).run();
    }
    db.prepare(
      `INSERT INTO admin_settings (key, value) VALUES ('password_hash', '')
       ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
    ).run();
  })();
  logStructured({ level: "INFO", category: "system", message: "factory reset completed" });
  publishAdminEvent("system.status");
  return sendSuccess(res, { ok: true, rebooting: false }, "Factory reset complete (simulator)");
});

systemRouter.post("/backup", async (_req, res) => {
  const backup = await createTimestampedBackup();
  publishAdminEvent("system.status");
  return sendSuccess(res, backup);
});

systemRouter.get("/db-health", (_req, res) => {
  const integrity = runIntegrityCheck();
  return sendSuccess(res, { integrity });
});

systemRouter.get("/network", (_req, res) => {
  return sendSuccess(res, buildSimNetworkPayload());
});

systemRouter.post("/management-ap/post-setup", (req, res) => {
  const keepEnabled = req.body?.keepEnabled === true;
  simManagementAp.keepEnabledAfterSetup = keepEnabled;
  if (keepEnabled) {
    simManagementAp.running = true;
    simManagementAp.enabled = true;
    simManagementAp.mode = "maintenance";
    simManagementAp.startedAtMs = Date.now();
  } else {
    simManagementAp.running = false;
    simManagementAp.enabled = false;
    simManagementAp.mode = "disabled";
    simManagementAp.startedAtMs = null;
  }
  return sendSuccess(
    res,
    { ...buildSimManagementAp(), keepEnabledAfterSetup: keepEnabled },
    keepEnabled ? "Management access point kept enabled" : "Management access point disabled",
  );
});

systemRouter.post("/management-ap/start", (_req, res) => {
  simManagementAp.running = true;
  simManagementAp.enabled = true;
  simManagementAp.mode = "maintenance";
  simManagementAp.startedAtMs = Date.now();
  return sendSuccess(res, buildSimManagementAp(), "Maintenance access point started");
});

systemRouter.post("/management-ap/stop", (_req, res) => {
  simManagementAp.running = false;
  simManagementAp.enabled = false;
  simManagementAp.mode = "disabled";
  simManagementAp.startedAtMs = null;
  return sendSuccess(res, buildSimManagementAp(), "Maintenance access point stopped");
});

systemRouter.post("/management-ap/temporary", (req, res) => {
  const durationSeconds =
    typeof req.body?.durationSeconds === "number" ? req.body.durationSeconds : 600;
  simManagementAp.running = true;
  simManagementAp.enabled = true;
  simManagementAp.mode = "maintenance";
  simManagementAp.startedAtMs = Date.now();
  return sendSuccess(
    res,
    { ...buildSimManagementAp(), durationSeconds },
    "Temporary maintenance access point started",
  );
});
