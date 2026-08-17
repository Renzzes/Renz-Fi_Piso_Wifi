#!/usr/bin/env node
/**
 * Static contract: AsyncTCP sales/router snapshot safety, Connected-only
 * HealthProbe suppression, Ethernet-gated Activate, session/admin/portal
 * integrity. Source-only — does not flash hardware.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const REPO = path.resolve(ROOT, "..");

const read = (p) => fs.readFileSync(p, "utf8");
const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const SESS = read(path.join(ROOT, "src", "SessionManager.cpp"));
const SESS_H = read(path.join(ROOT, "src", "SessionManager.h"));
const APP = read(path.join(ROOT, "src", "FirmwareApp.cpp"));
const STOR = read(path.join(ROOT, "src", "StorageManager.cpp"));
const STOR_H = read(path.join(ROOT, "src", "StorageManager.h"));
const PROMO = read(path.join(ROOT, "src", "PromoManager.cpp"));
const PROMO_H = read(path.join(ROOT, "src", "PromoManager.h"));
const PSM = read(path.join(ROOT, "src", "PortalSessionManager.cpp"));
const PSM_H = read(path.join(ROOT, "src", "PortalSessionManager.h"));
const WORKER_C = read(path.join(ROOT, "src", "RouterProvisioningWorker.cpp"));
const WORKER_H = read(path.join(ROOT, "src", "RouterProvisioningWorker.h"));
const PLATFORM = read(path.join(ROOT, "src", "router", "RouterPlatform.cpp"));
const PLATFORM_H = read(path.join(ROOT, "src", "router", "RouterPlatform.h"));
const CFG = read(path.join(ROOT, "src", "Config.h"));
const RGB = read(path.join(ROOT, "src", "RgbController.cpp"));
const PART = read(path.join(ROOT, "partitions_custom.csv"));
const PIO = read(path.join(ROOT, "platformio.ini"));

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

function extractStatusHandler(api) {
  const start = api.indexOf('_server->on("/api/status"');
  if (start < 0) throw new Error("GET /api/status handler missing");
  const next = api.indexOf("_server->on(", start + 10);
  return api.slice(start, next > start ? next : start + 8000);
}

function extractHealthHandler(api) {
  const start = api.indexOf('_server->on("/api/health"');
  if (start < 0) throw new Error("GET /api/health handler missing");
  const next = api.indexOf("_server->on(", start + 10);
  return api.slice(start, next > start ? next : start + 6000);
}

function extractSystemHealthHandler(api) {
  const start = api.indexOf('_server->on("/api/system/health"');
  if (start < 0) throw new Error("GET /api/system/health handler missing");
  const next = api.indexOf("_server->on(", start + 10);
  return api.slice(start, next > start ? next : start + 3000);
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

const statusHandler = extractStatusHandler(API);
const healthHandler = extractHealthHandler(API);
const systemHealthHandler = extractSystemHealthHandler(API);

check("A1 /api/status does not read sales.json or router.json", () => {
  mustNotContain("no SALES_FILE in status", statusHandler, "SALES_FILE");
  mustNotContain("no readJson in status", statusHandler, "readJson");
  mustNotContain("no writeJson in status", statusHandler, "writeJson");
  mustNotContain("no router load in status", statusHandler, String.raw`->load\(`);
  mustContain("sales from RAM helpers", statusHandler, "salesToday");
  mustContain("router from cache", statusHandler, "cachedRouterConfigured");
  mustContain("router host from cache", statusHandler, "cachedRouterHost");
});

check("A1b /api/status performs zero live filesystem I/O", () => {
  mustNotContain("no fileSizeBytes", statusHandler, "fileSizeBytes");
  mustNotContain("no fillSdStatus", statusHandler, "fillSdStatus");
  mustNotContain("no fillStorageStatus", statusHandler, "fillStorageStatus");
  mustNotContain("no getSpiffsUsedBytes", statusHandler, "getSpiffsUsedBytes");
  mustNotContain("no getSpiffsTotalBytes", statusHandler, "getSpiffsTotalBytes");
  mustNotContain("no mergedActiveUserStats", statusHandler, "mergedActiveUserStats");
  mustNotContain("no appendActiveUsers", statusHandler, "appendActiveUsers");
  mustNotContain("no SD.open", statusHandler, String.raw`SD\.open`);
  mustNotContain("no SD.exists", statusHandler, String.raw`SD\.exists`);
  mustNotContain("no SD.usedBytes", statusHandler, String.raw`SD\.usedBytes`);
  mustNotContain("no SD.totalBytes", statusHandler, String.raw`SD\.totalBytes`);
  mustNotContain("no STORAGE_LOCK", statusHandler, "ScopedStorageLock");
  mustNotContain("no lockStorage", statusHandler, "lockStorage");
  mustContain("dashboard RAM snapshot", statusHandler, "fillDashboardStatus");
  mustContain("active users from RAM", statusHandler, "cachedActiveUserStats");
  mustContain("slow-handler diagnostic", statusHandler, "http-status");
});

check("A1c snapshot strings use strlen-safe JSON assignment", () => {
  mustContain("jsonCString helper", STOR, "jsonCString");
  mustContain(
    "storageMode via jsonCString",
    STOR,
    String.raw`storage\["storageMode"\] = jsonCString`,
  );
  mustContain(
    "health via jsonCString",
    STOR,
    String.raw`storage\["health"\] = jsonCString`,
  );
  mustContain(
    "diagnosticCause via jsonCString",
    STOR,
    String.raw`storage\["diagnosticCause"\] = jsonCString`,
  );
  mustNotContain(
    "no raw const-array storageMode assign",
    STOR,
    String.raw`storage\["storageMode"\] = snap.storageMode`,
  );
  mustNotContain(
    "no raw const-array health assign",
    STOR,
    String.raw`storage\["health"\] = snap.health`,
  );
});

check("A1d /api/status keeps legacy dashboard fields; health stays RAM snapshot", () => {
  mustContain("sales today", statusHandler, String.raw`\["today"\]`);
  mustContain("activeUsers count", statusHandler, String.raw`\["activeUsers"\]\["count"\]`);
  mustContain("coinSlot", statusHandler, "coinSlot");
  mustContain("mikrotik", statusHandler, "mikrotik");
  mustContain("hotspot", statusHandler, "hotspot");
  mustContain("storage flash", statusHandler, "fillDashboardStatus");
  mustContain("esp32 uptime", statusHandler, String.raw`\["esp32"\]\["uptime"\]`);
  mustContain("system health fillHealth", systemHealthHandler, "fillHealth");
  mustNotContain("system health no fileSizeBytes", systemHealthHandler, "fileSizeBytes");
  mustNotContain("system health no SD.open", systemHealthHandler, String.raw`SD\.open`);
  mustNotContain("system health no readJson", systemHealthHandler, "readJson");
  mustNotContain("system health no fillSdStatus", systemHealthHandler, "fillSdStatus");
});

check("A2 salesToday/Week/Month copy RAM snapshot only", () => {
  mustContain("snapshot copy helper", SESS, "copySalesSummarySnapshot");
  mustContain(
    "salesToday uses snapshot",
    SESS,
    String.raw`bool SessionManager::salesToday[\s\S]{0,250}?copySalesSummarySnapshot`,
  );
  mustContain(
    "salesWeek uses snapshot",
    SESS,
    String.raw`bool SessionManager::salesWeek[\s\S]{0,250}?copySalesSummarySnapshot`,
  );
  mustContain(
    "salesMonth uses snapshot",
    SESS,
    String.raw`bool SessionManager::salesMonth[\s\S]{0,250}?copySalesSummarySnapshot`,
  );
  mustNotContain(
    "salesToday no SD read",
    SESS,
    String.raw`bool SessionManager::salesToday[\s\S]{0,400}?readJson`,
  );
  mustNotContain(
    "salesToday no SalesLock",
    SESS,
    String.raw`bool SessionManager::salesToday[\s\S]{0,200}?SalesLock`,
  );
});

check("A3 loopTask refreshes sales snapshot; stale cache is served", () => {
  mustContain("refresh API", SESS_H, "refreshSalesSummarySnapshot");
  mustContain(
    "health snapshot refreshes sales",
    APP,
    String.raw`void FirmwareApp::refreshHealthSnapshots[\s\S]{0,500}?refreshSalesSummarySnapshot`,
  );
  mustContain(
    "lock busy keeps last snapshot",
    SESS,
    "serving_last_snapshot",
  );
  mustContain("dirty on mutation", SESS, "markSalesSummaryDirty");
  mustContain(
    "one-pass aggregate",
    SESS,
    "aggregateAllSales",
  );
});

check("A4 loopTask publishes storage + active-user RAM snapshots", () => {
  mustContain("dashboard snapshot API", STOR_H, "fillDashboardStatus");
  mustContain(
    "runtime snapshot publishes dashboard",
    STOR,
    "publishDashboardSnapUnlocked",
  );
  mustContain(
    "logs size stays on loopTask",
    STOR,
    String.raw`void StorageManager::refreshRuntimeSnapshot[\s\S]{0,4000}?LOGS_FILE`,
  );
  mustNotContain(
    "fillStorageStatus no STORAGE_LOCK",
    STOR,
    String.raw`void StorageManager::fillStorageStatus[\s\S]{0,200}?ScopedStorageLock`,
  );
  mustContain("active-user snapshot API", SESS_H, "refreshMergedActiveUserSnapshot");
  mustContain("active-user HTTP copy", SESS_H, "cachedActiveUserStats");
  mustContain(
    "health snapshot refreshes active users",
    APP,
    String.raw`void FirmwareApp::refreshHealthSnapshots[\s\S]{0,600}?refreshMergedActiveUserSnapshot`,
  );
});

check("A5 promo cache is preloaded; coin/list paths are RAM-only", () => {
  mustContain("boot preload", PROMO, String.raw`void PromoManager::begin[\s\S]{0,250}?ensureCacheLoaded`);
  mustContain("ensureCacheLoaded API", PROMO_H, "ensureCacheLoaded");
  mustNotContain(
    "list does not load SD",
    PROMO,
    String.raw`bool PromoManager::list[\s\S]{0,250}?loadCache`,
  );
  mustContain(
    "loopTask retries promo cache",
    APP,
    String.raw`void FirmwareApp::refreshHealthSnapshots[\s\S]{0,700}?ensureCacheLoaded`,
  );
  mustContain(
    "coin still uses resolveForAmount",
    PSM,
    String.raw`void PortalSessionManager::onCoinInserted[\s\S]{0,400}?resolveForAmount`,
  );
});

check("B1 Verify login timeout does not HealthProbe for Connected-only", () => {
  mustContain("recovery probe helper", PSM_H, "needsHealthRecoveryProbe");
  mustContain(
    "probe gated on recovery work",
    PSM,
    String.raw`wantsHealthProbe\(now\) &&[\s\S]{0,80}?needsHealthRecoveryProbe\(\)`,
  );
  mustNotContain(
    "old Connected-or-work probe gate",
    PSM,
    String.raw`wantsHealthProbe\(now\) && needsRouterOsWork\(\)`,
  );
  mustContain(
    "Connected still counts as router work",
    PSM,
    String.raw`bool PortalSessionManager::needsRouterOsWork[\s\S]{0,2500}?PortalState::Active[\s\S]{0,400}?session\["connected"\]`,
  );
  mustNotContain(
    "recovery probe ignores Connected-only",
    PSM,
    String.raw`bool PortalSessionManager::needsHealthRecoveryProbe[\s\S]{0,1600}?connected`,
  );
  mustContain(
    "suppress log",
    PSM,
    "verify-login-timeout connected=1 probe_suppressed=1",
  );
});

check("B2 Critical jobs remain executable; one worker", () => {
  mustContain("activate still allowed degraded", WORKER_C, "allowsHotspotActivate");
  mustContain("single enqueueFireAndForget", WORKER_C, "enqueueFireAndForget");
  mustNotContain("no second worker", WORKER_C, "router_worker_2");
  mustContain("one RouterWorker type", WORKER_H, "class RouterProvisioningWorker");
});

check("C1 Activate cannot dispatch without Ethernet IP", () => {
  mustContain("ethernet ready helper", WORKER_H, "ethernetReadyForHotspot");
  mustContain(
    "activate checks ethernet first",
    WORKER_C,
    String.raw`tryEnqueueActivateHotspotUser[\s\S]{0,500}?ethernetReadyForHotspot`,
  );
  mustContain("reject 0.0.0.0", WORKER_C, String.raw`ip != "0.0.0.0"`);
  mustContain("deferred log", WORKER_C, "ethernet_not_ready");
});

check("C2 pending entitlement survives; one Activate after ETH_GOT_IP", () => {
  mustContain(
    "enqueue fail keeps pending",
    PSM,
    String.raw`activationRetryPending"\] = true`,
  );
  mustContain(
    "retry skips duplicate authorized",
    PSM,
    String.raw`alreadyAuthorizedThisGeneration\(session\)`,
  );
  mustContain(
    "1s idle retry uses existing tick",
    PSM,
    String.raw`now - _lastTickMs >= 1000[\s\S]{0,250}?retryPendingRouterWork`,
  );
  mustContain(
    "retry does not enqueue while ethernet down",
    PSM,
    String.raw`ethernetReadyForHotspot\(\)`,
  );
});

check("D session integrity freeze", () => {
  mustContain("generation helper", PSM, "sessionGenerationOf");
  mustContain("already authorized skip", PSM, "already authorized — skip duplicate");
  mustContain("clock helper remains", PSM, "commitAuthorizedClockUnlocked");
  mustNotContain("status does not set connected", statusHandler, "connected");
});

check("E Admin /api/health stays snapshot-light", () => {
  mustContain("health uses fillHealthStatus", healthHandler, "fillHealthStatus");
  mustContain("health uses fillStorageStatus", healthHandler, "fillStorageStatus");
  mustNotContain("health no salesToday", healthHandler, "salesToday");
  mustNotContain("health no sales.json", healthHandler, "SALES_FILE");
  mustNotContain("health no aggregate", healthHandler, "aggregateAllSales");
});

check("F Portal session/heartbeat stay local", () => {
  mustContain(
    "getSession local",
    PSM,
    String.raw`bool PortalSessionManager::getSession[\s\S]{0,400}?lockState`,
  );
  mustNotContain(
    "getSession no RouterOS",
    PSM,
    String.raw`bool PortalSessionManager::getSession[\s\S]{0,800}?tryEnqueue`,
  );
  mustNotContain(
    "heartbeat no RouterOS",
    PSM,
    String.raw`bool PortalSessionManager::heartbeat[\s\S]{0,800}?tryEnqueue`,
  );
});

check("G Watchdog: no sales aggregation or TWDT change on HTTP", () => {
  mustNotContain("no TWDT config", CFG, "TASK_WDT");
  mustNotContain("no TWDT ini", PIO, "TASK_WDT");
  mustNotContain("no wdt feed in status", statusHandler, "esp_task_wdt");
  mustNotContain("no delay in status", statusHandler, String.raw`delay\(`);
  mustContain("rgb remains fillStatus", RGB, "RgbController::fillStatus");
  mustContain(
    "router cache refresh stays loopTask",
    PLATFORM,
    String.raw`void RouterPlatform::refreshHealthCache[\s\S]{0,250}?load\(`,
  );
  mustContain("cached host accessor", PLATFORM_H, "cachedRouterHost");
});

check("H core dump partition present for usable crash dumps", () => {
  mustContain("coredump partition", PART, "coredump");
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} ESP32/MikroTik stability contracts PASS`
    : `\n${failed}/${checks.length} ESP32/MikroTik stability contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
