#!/usr/bin/env node
/**
 * Static contract: Factory Reset is asynchronous (not on async_tcp),
 * returns 202+jobId, and clears owner + setup-unlock credentials.
 * Source-only — does not flash hardware.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const REPO = path.resolve(ROOT, "..");

const read = (p) => fs.readFileSync(p, "utf8");
const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const WORKER = read(path.join(ROOT, "src", "FactoryResetWorker.cpp"));
const WORKER_H = read(path.join(ROOT, "src", "FactoryResetWorker.h"));
const APP = read(path.join(ROOT, "src", "FirmwareApp.cpp"));
const RPM = read(path.join(ROOT, "src", "RouterProvisioningManager.cpp"));
const PROV = read(path.join(ROOT, "src", "SetupProvisioningManager.cpp"));
const STORAGE = read(path.join(ROOT, "src", "StorageManager.cpp"));
const AUTH = read(path.join(ROOT, "src", "AuthManager.cpp"));
const STATUS = read(path.join(ROOT, "src", "StorageManager.cpp"));
const FRONT = read(path.join(REPO, "src", "pages", "SystemSettingsPage.tsx"));
const SYS = read(path.join(REPO, "src", "services", "system.ts"));

function mustContain(label, text, pattern) {
  if (!new RegExp(pattern, "ms").test(text)) {
    throw new Error(`missing: ${label} (${pattern})`);
  }
}

function mustNotContain(label, text, pattern) {
  if (new RegExp(pattern, "ms").test(text)) {
    throw new Error(`forbidden: ${label} (${pattern})`);
  }
}

const checks = [];
function check(name, fn) {
  try {
    fn();
    console.log(`PASS ${name}`);
    checks.push(true);
  } catch (exc) {
    console.log(`FAIL ${name}: ${exc.message || exc}`);
    checks.push(false);
  }
}

function factoryResetPostHandler() {
  const start = API.indexOf('_server->on(\n      "/api/system/factory-reset", HTTP_POST');
  const alt = API.indexOf('"/api/system/factory-reset", HTTP_POST');
  const from = start >= 0 ? start : alt;
  if (from < 0) throw new Error("POST /api/system/factory-reset missing");
  const next = API.indexOf("_server->on(", from + 20);
  return API.slice(from, next > from ? next : from + 2500);
}

check("1 Factory Reset endpoint exists", () => {
  mustContain("POST factory-reset", API, String.raw`"/api/system/factory-reset", HTTP_POST`);
});

check("2 Endpoint requires owner authorization", () => {
  const handler = factoryResetPostHandler();
  mustContain("owner auth", handler, "requireOwnerAuth");
});

check("3 Endpoint returns 202", () => {
  const handler = factoryResetPostHandler();
  mustContain("HTTP 202", handler, String.raw`beginResponse\(202`);
});

check("4 Endpoint returns jobId", () => {
  const handler = factoryResetPostHandler();
  mustContain("jobId", handler, String.raw`data\["jobId"\]`);
  mustContain("queued", handler, String.raw`data\["status"\]\s*=\s*"queued"`);
});

check("5 Reset work is not synchronous in HTTP callback", () => {
  const handler = factoryResetPostHandler();
  mustContain("enqueue only", handler, "enqueue");
  mustNotContain("no performFactoryReset in HTTP", handler, "performFactoryReset");
  mustNotContain("no wipeUserData in HTTP", handler, "wipeUserData");
  mustNotContain("no deleteAsset in HTTP", handler, "deleteAsset");
  mustNotContain("no ESP.restart in HTTP", handler, "ESP.restart");
  mustNotContain("no delay in HTTP", handler, String.raw`delay\(`);
  mustContain("worker runs from loopTask", APP, "_factoryReset.loop");
  mustContain("worker class", WORKER_H, "class FactoryResetWorker");
});

check("6 Job status endpoint exists", () => {
  mustContain(
    "GET status",
    API,
    String.raw`"/api/system/factory-reset/status", HTTP_GET`,
  );
  mustContain("status owner auth", API, String.raw`factory-reset/status[\s\S]{0,400}?requireOwnerAuth`);
});

check("7 Owner state is cleared", () => {
  mustContain("quiesce clears owner", PROV, "beginFactoryResetQuiesce");
  mustContain("ownerCreated false", PROV, String.raw`_ownerCreated\s*=\s*false`);
  mustContain("auth NVS reset", WORKER, String.raw`resetToDefault\(false\)`);
  mustContain("firstBootCompleted false", AUTH, "_firstBootCompleted = false");
});

check("8 Installation state returns to Factory", () => {
  mustContain("resetToFactory", WORKER, "resetToFactory");
  mustContain("validate Factory", WORKER, "InstallationState::Factory");
});

check("9 Setup wizard returns to first step", () => {
  mustContain("clear wizard RAM", WORKER, "clearForFactoryReset");
  mustContain("delete setup wizard file", WORKER, "SetupWizardFile");
});

check("10 Setup unlock session is cleared", () => {
  mustContain("lockSetup in quiesce", PROV, String.raw`beginFactoryResetQuiesce[\s\S]{0,300}?lockSetup`);
  mustContain("session gone in validate", WORKER, "hasActiveSetupUnlockSession");
});

check("11 setupUnlockPasswordHash is cleared", () => {
  mustContain("hash cleared", PROV, String.raw`_setupUnlockPasswordHash\s*=\s*""`);
  mustContain("provisioning.json deleted", WORKER, "ProvisioningFile");
  mustContain("credentials cleared helper", PROV, "factoryResetCredentialsCleared");
});

check("12 setupUnlockPasswordProtected is cleared", () => {
  mustContain("blob cleared", PROV, String.raw`_setupUnlockPasswordProtected\s*=\s*""`);
  mustContain("validate blob empty", PROV, "_setupUnlockPasswordProtected.isEmpty");
});

check("13 Router credentials are cleared", () => {
  mustContain("router connection file", WORKER, "RouterConnectionFile");
  mustContain("router RAM clear", WORKER, "_routerConnection->clearForFactoryReset");
  mustContain("provisioning file", WORKER, "RouterProvisioningFile");
  mustContain("provisioning RAM clear", WORKER, "_routerProvisioning->clearForFactoryReset");
});

check("14 Duplicate reset requests are rejected", () => {
  const handler = factoryResetPostHandler();
  mustContain("busy enqueue 0", WORKER, String.raw`if \(busy\(\)\) return 0`);
  mustContain("409 in progress", handler, "FACTORY_RESET_IN_PROGRESS");
  mustContain("409 status", handler, String.raw`sendError\(req, 409`);
});

check("15 Frontend polls the job", () => {
  mustContain("status client", SYS, "factoryResetStatus");
  mustContain("poll loop", FRONT, String.raw`factoryResetStatus\(jobId\)`);
});

check("16 Frontend handles intentional reboot", () => {
  mustContain("restart copy", FRONT, "Factory reset completed. The device is restarting.");
  mustContain("setup AP redirect", FRONT, "http://192.168.4.1");
  mustContain("in progress copy", FRONT, "Factory Reset in progress");
  mustContain("network error after reboot", FRONT, "isNetworkError");
  mustContain("401 after session invalidation", FRONT, "error.status === 401");
});

check("17 Firmware partition is never erased", () => {
  mustNotContain("no partition erase", WORKER, "erasePartition");
  mustNotContain("no esp_partition_erase", WORKER, "esp_partition_erase");
  mustNotContain("no SPIFFS.format", WORKER, "SPIFFS.format");
  mustNotContain("no SD.format", WORKER, "SD.format");
});

check("18 SPIFFS/Setup assets remain bootable", () => {
  mustNotContain("no /admin delete", WORKER, String.raw`SPIFFS\.remove\("/admin`);
  mustNotContain("no /portal delete", WORKER, String.raw`SPIFFS\.remove\("/portal`);
  mustContain("fallback user data only", STORAGE, "clearAllFallbackData");
});

check("19 /api/status remains RAM-only", () => {
  mustNotContain("factory reset not in status", API, String.raw`fillDashboardStatus[\s\S]{0,400}?factoryReset`);
  mustContain("skip snapshots while reset busy", APP, String.raw`_productionRegistered && !_factoryReset.busy`);
  mustContain("skip storage poll while busy", APP, String.raw`if \(!_factoryReset.busy\(\)\) \{[\s\S]*_storage.pollStorageHealth`);
});

check("20 /api/system/health remains snapshot-based", () => {
  mustNotContain("no factory reset in health handler", API, String.raw`/api/system/health[\s\S]{0,800}?factoryReset`);
});

check("21 Wi-Fi selection persist runs on loopTask", () => {
  mustContain("router provisioning loop", APP, String.raw`_routerProvisioning\.loop\(`);
  mustContain(
    "skip persist during factory reset",
    APP,
    String.raw`if \(!_factoryReset.busy\(\)\) \{[\s\S]*_routerProvisioning\.loop\(`,
  );
});

check("22 Factory reset invalidates sessions at reboot", () => {
  mustContain("keep poll session during job", WORKER, String.raw`resetToDefault\(false\)`);
  mustContain("invalidate at reboot", WORKER, String.raw`resetToDefault\(true\)`);
});

check("23 Factory reset cancels persist before file delete", () => {
  const runStep = WORKER.indexOf("void FactoryResetWorker::runStep()");
  const quiesce = WORKER.indexOf("case Step::Quiesce:", runStep);
  const clearProv = WORKER.indexOf(
    "if (_routerProvisioning) _routerProvisioning->clearForFactoryReset();",
    quiesce,
  );
  const deleteFiles = WORKER.indexOf("case Step::DeleteFiles:", runStep);
  if (runStep < 0 || quiesce < 0 || clearProv < 0 || deleteFiles < 0) {
    throw new Error("Quiesce / clearForFactoryReset / DeleteFiles missing");
  }
  if (!(quiesce < clearProv && clearProv < deleteFiles)) {
    throw new Error(
      "clearForFactoryReset must run in Quiesce before DeleteFiles",
    );
  }
  mustContain("enqueue cancels persist", WORKER,
    String.raw`enqueue\(\)[\s\S]*clearForFactoryReset`);
  mustContain("loop refuses after quiesce", RPM, String.raw`if \(_factoryResetQuiesced\) return`);
  mustContain("persist refuses after quiesce", RPM,
    String.raw`persist suppressed \(factory reset\)`);
});

check("24 Wi-Fi selection stays deferred off async_tcp", () => {
  const start = RPM.indexOf("RouterProvisioningManager::saveWifiSelection");
  const end = RPM.indexOf("RouterProvisioningManager::configureExistingNetwork", start);
  if (start < 0 || end <= start) {
    throw new Error("saveWifiSelection body not found");
  }
  const body = RPM.slice(start, end);
  if (/\bpersist\s*\(/.test(body)) {
    throw new Error("saveWifiSelection must not call persist() on async_tcp");
  }
  if (!body.includes("scheduleDeferredPersist()")) {
    throw new Error("saveWifiSelection must scheduleDeferredPersist()");
  }
  if (!body.includes("202")) {
    throw new Error("saveWifiSelection must return HTTP 202 while queued");
  }
  mustContain("new SSID complete without interfaceId", RPM,
    String.raw`wifiSetupComplete[\s\S]*kModeNew[\s\S]*!_wifiSsid\.isEmpty`);
});

check("25 Communication quiesce while factory reset busy", () => {
  mustContain("bindFactoryReset", APP, "HttpPlaneGate::bindFactoryReset");
  const GATE = read(path.join(ROOT, "src", "web", "HttpPlaneGate.cpp"));
  const GATE_H = read(path.join(ROOT, "src", "web", "HttpPlaneGate.h"));
  mustContain("ensureNotFactoryResetting", GATE_H, "ensureNotFactoryResetting");
  mustContain("allow-list status", GATE, "factory-reset/status");
  mustContain("409 FACTORY_RESET_IN_PROGRESS", GATE, "FACTORY_RESET_IN_PROGRESS");
  mustContain("gate in ensureProductionPlane", GATE,
    String.raw`ensureProductionPlane[\s\S]*ensureNotFactoryResetting`);
  mustContain("gate in ensureSetupPlane", GATE,
    String.raw`ensureSetupPlane[\s\S]*ensureNotFactoryResetting`);
  mustContain("gate in ensureAppliancePlane", GATE,
    String.raw`ensureAppliancePlane[\s\S]*ensureNotFactoryResetting`);
  mustContain("close SSE on enqueue", API, "closeAllClients");
  const EVENTS = read(path.join(ROOT, "src", "EventBus.cpp"));
  mustContain("SSE heartbeat skips when busy", EVENTS,
    String.raw`heartbeat\(\)[\s\S]*isFactoryResetBusy`);
  mustContain("SSE emit skips when busy", EVENTS,
    String.raw`emit\([\s\S]*isFactoryResetBusy`);
  const FRONT_QUIESCE = read(path.join(REPO, "src", "services", "factoryResetQuiesce.ts"));
  mustContain("frontend quiesce module", FRONT_QUIESCE, "setFactoryResetQuiesced");
  mustContain("frontend sets quiesce on reset", FRONT, "setFactoryResetQuiesced\\(true\\)");
});

check("26 SSE reject before AsyncEventSourceClient construction", () => {
  const EVENTS = read(path.join(ROOT, "src", "EventBus.cpp"));
  mustContain(
    "authorizeConnect pre-construction gate",
    EVENTS,
    "authorizeConnect",
  );
  mustContain(
    "log before construction",
    EVENTS,
    "SSE rejected before client construction",
  );
  mustContain(
    "busy check in authorizeConnect",
    EVENTS,
    String.raw`authorizeConnect[\s\S]*isFactoryResetBusy`,
  );
  // Regression: never reintroduce close() from onConnect (ctor reentrancy).
  const onConnectIdx = EVENTS.indexOf("_source->onConnect(");
  if (onConnectIdx < 0) throw new Error("onConnect missing");
  const onConnectEnd = EVENTS.indexOf("});", onConnectIdx);
  const onConnectBody = EVENTS.slice(
    onConnectIdx,
    onConnectEnd > onConnectIdx ? onConnectEnd : onConnectIdx + 800,
  );
  mustNotContain(
    "no client->close in onConnect",
    onConnectBody,
    String.raw`client\s*->\s*close\s*\(`,
  );
  mustNotContain(
    "no close() in onConnect busy branch",
    onConnectBody,
    String.raw`rejected SSE connect`,
  );
  mustContain("closeAllClients retained", EVENTS, "closeAllClients");
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} factory-reset contracts PASS`
    : `\n${failed}/${checks.length} factory-reset contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
