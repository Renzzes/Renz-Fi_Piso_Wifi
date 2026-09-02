#!/usr/bin/env node
/**
 * Static contract: Admin is an optional Core client.
 * Sync = Core state (not credential dump / not RouterOS-on-every-connect).
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const REPO = path.resolve(ROOT, "..");
const read = (p) => fs.readFileSync(p, "utf8");

const APP = read(path.join(REPO, "src", "App.tsx"));
const SYNC = read(path.join(REPO, "src", "services", "adminSync.ts"));
const SCREEN = read(path.join(REPO, "src", "components", "AdminSyncScreen.tsx"));
const EVENTS = read(path.join(REPO, "src", "hooks", "useDashboardEvents.ts"));
const MONITOR = read(path.join(REPO, "src", "hooks", "useAdminApiMonitor.ts"));
const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const BUS = read(path.join(ROOT, "src", "EventBus.cpp"));
const SALES = read(path.join(ROOT, "src", "SessionManager.cpp"));
const FWAPP = read(path.join(ROOT, "src", "FirmwareApp.cpp"));
const STORAGE = read(path.join(ROOT, "src", "StorageManager.cpp"));
const CACHE = read(path.join(ROOT, "src", "RouterCacheManager.cpp"));
const AUTH = read(path.join(ROOT, "src", "AuthManager.cpp"));
const DOCS = read(path.join(REPO, "docs", "ADMIN_CORE_ISOLATION.md"));

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

check("1 No POST /api/admin/sync RouterOS job", () => {
  mustNotContain("no admin sync route", API, String.raw`/api/admin/sync`);
});

check("2 Connect sync always reads Core status", () => {
  mustContain("status snapshot", SYNC, "systemApi.status");
  mustNotContain("no router test on connect", SYNC, "routerApi.test");
  mustNotContain("no /api/router/test", SYNC, "/api/router/test");
});

check("3 Connect never auto-enqueues RouterOS cache/sync", () => {
  mustContain("status snapshot", SYNC, "systemApi.status");
  mustNotContain("no syncRouter on connect", SYNC, "routerApi.syncRouter");
  mustNotContain("no router test on connect", SYNC, "routerApi.test");
  mustContain("worker enqueue exists for button path", API, String.raw`/api/router/cache/sync`);
  mustContain("enqueueAdminSyncCache", API, "enqueueAdminSyncCache");
  // Thin connect: workerRefreshRequested is always false (no auto enqueue).
  mustContain("no auto worker refresh", SYNC, "workerRefreshRequested: false");
});

check("4 Accurate sync wording (no credential transfer claim)", () => {
  mustContain("Renz-Fi state", SYNC, "Synchronizing Renz-Fi state");
  mustNotContain("no credentials label", SYNC, "Synchronizing MikroTik credentials");
  mustNotContain("no connecting Mikrotik claim", SYNC, "Connecting to MikroTik");
  mustContain("credentials stay on ESP32", SCREEN, "credentials stay on the ESP32");
});

check("5 App connect runs sync before dashboard", () => {
  mustContain("synchronizeAdminClient", APP, "synchronizeAdminClient");
  mustContain("AdminSyncScreen", APP, "AdminSyncScreen");
  mustNotContain("no test on connect in App", APP, "routerApi.test");
});

check("6 EventBus no-ops without clients", () => {
  mustContain("zero clients", BUS, String.raw`if \(_source->count\(\) == 0\) return`);
});

check("7 sale.created only after persist", () => {
  const start = SALES.indexOf("bool SessionManager::upsertSale");
  const body = SALES.slice(start, start + 1800);
  if (body.indexOf("saveSalesBoundedLocked") < 0) {
    throw new Error("upsertSale must persist first");
  }
  if (body.indexOf('emit("sale.created"') < 0) {
    throw new Error("upsertSale must emit sale.created");
  }
  if (body.indexOf("saveSalesBoundedLocked") > body.indexOf('emit("sale.created"')) {
    throw new Error("sale.created must be after persist");
  }
});

check("8 sale.created uses targeted UI patch", () => {
  mustContain("setQueryData patch", EVENTS, "setQueryData");
  mustContain("applySaleCreatedPatch", EVENTS, "applySaleCreatedPatch");
  mustContain("sale.created listener", EVENTS, String.raw`addEventListener\("sale.created"`);
});

check("9 Health poll skipped while SSE live or Admin standby", () => {
  mustContain("sseConnected option", MONITOR, "sseConnected");
  mustContain("standbyIdle option", MONITOR, "standbyIdle");
  mustContain("skip interval when SSE or standby", MONITOR, String.raw`if \(sseConnected \|\| standbyIdle\)`);
  mustContain("no interval while SSE", MONITOR, "HEALTH_POLL_MS");
});

check("10 Setup Wi-Fi persist loop still present", () => {
  mustContain("deferred persist", FWAPP, String.raw`_routerProvisioning\.loop\(`);
  mustContain("skip while factory reset", FWAPP, String.raw`if \(!_factoryReset.busy\(\)\)`);
});

check("11 Sales fallback path exists in StorageManager", () => {
  mustContain("SPIFFS fallback sales", STORAGE, "Sales storage = SPIFFS fallback");
  mustContain("writeJson dual path", STORAGE, "writeJsonToSpiffs");
});

check("12 Router cache freshness exists", () => {
  mustContain("isStale", CACHE, "bool RouterCacheManager::isStale");
  mustContain("docs semantics", DOCS, "does not create a new RouterOS login on every Admin connection");
});

check("13 No plaintext password in status path", () => {
  const statusIdx = API.indexOf("fillDashboardStatus") >= 0
    ? API.indexOf('"/api/status"')
    : API.indexOf("/api/status");
  const slice = API.slice(statusIdx, statusIdx + 8000);
  mustNotContain("no password in status slice", slice, String.raw`\["password"\]\s*=`);
  mustNotContain("no passwordProtected in status", slice, "passwordProtected");
});

check("14 Admin login uses infoLocal (no durable SD history on async_tcp)", () => {
  const loginIdx = AUTH.indexOf("bool AuthManager::login");
  const loginBody = AUTH.slice(loginIdx, loginIdx + 2500);
  mustContain("infoLocal after login", loginBody, String.raw`infoLocal\("auth", "Login successful"\)`);
  mustNotContain("no durable info on login success", loginBody, String.raw`->info\("auth", "Login successful"\)`);
  mustContain("saveSession is memory-only", AUTH, "rememberInMemory");
  mustContain("saveSession returns rememberInMemory", AUTH, String.raw`return rememberInMemory\(token`);
});

check("15 Admin login route has no RouterOS / writeJson / sync wait", () => {
  const routeIdx = API.indexOf('"/api/auth/login"');
  if (routeIdx < 0) throw new Error("missing /api/auth/login route");
  const routeBody = API.slice(routeIdx, routeIdx + 1200);
  mustContain("calls AuthManager::login", routeBody, String.raw`_auth->login\(`);
  mustNotContain("no RouterOS in login route", routeBody, "RouterOS|MikroTik|enqueueAdminSyncCache|writeJson|appendHistory|syncRouter");
  mustNotContain("no cache sync in login", routeBody, "cache/sync");
});

check("16 Heavy storage snapshot walks are interval-throttled", () => {
  const fnIdx = STORAGE.indexOf("void StorageManager::refreshRuntimeSnapshot");
  const body = STORAGE.slice(fnIdx, fnIdx + 4500);
  mustContain("heavy interval gate", body, "STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS");
  const heavyStart = body.indexOf("STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS");
  const heavyBlock = body.slice(heavyStart, heavyStart + 2200);
  mustContain("fallbackTotalBytes inside throttle", heavyBlock, "fallbackTotalBytes");
  mustContain("countSpoolRecords inside throttle", heavyBlock, "countSpoolRecords");
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} admin-core isolation contracts PASS`
    : `\n${failed}/${checks.length} admin-core isolation contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
