#!/usr/bin/env node
/**
 * Phase 3B contract: Admin Sync = Configuration; Refresh = Telemetry.
 * Forbidden: WAN repair / captive reconcile / route print on normal Sync/Refresh.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const REPO = path.resolve(ROOT, "..");
const read = (p) => fs.readFileSync(p, "utf8");

const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const WORKER_H = read(path.join(ROOT, "src", "RouterProvisioningWorker.h"));
const WORKER = read(path.join(ROOT, "src", "RouterProvisioningWorker.cpp"));
const PLATFORM = read(path.join(ROOT, "src", "router", "RouterPlatform.cpp"));
const DRIVER = read(path.join(ROOT, "src", "router", "drivers", "MikroTikDriver.cpp"));
const IRH = read(path.join(ROOT, "src", "router", "IRouterDriver.h"));
const CACHE = read(path.join(ROOT, "src", "RouterCacheManager.cpp"));
const CFG = read(path.join(ROOT, "src", "Config.h"));
const SYNC = read(path.join(REPO, "src", "services", "adminSync.ts"));
const ROUTER_TS = read(path.join(REPO, "src", "services", "router.ts"));

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

/** Extract function body starting at first '{' after needle. */
function extractFunction(src, signatureNeedle) {
  const sig = src.indexOf(signatureNeedle);
  if (sig < 0) throw new Error(`signature not found: ${signatureNeedle}`);
  const brace = src.indexOf("{", sig);
  if (brace < 0) throw new Error(`no body for: ${signatureNeedle}`);
  let depth = 0;
  for (let i = brace; i < src.length; i++) {
    const ch = src[i];
    if (ch === "{") depth++;
    else if (ch === "}") {
      depth--;
      if (depth === 0) return src.slice(brace, i + 1);
    }
  }
  throw new Error(`unbalanced body for: ${signatureNeedle}`);
}

// Strip C/C++ comments so retained-for-future notes do not fail contracts.
function stripComments(src) {
  return src
    .replace(/\/\*[\s\S]*?\*\//g, " ")
    .replace(/\/\/[^\n]*/g, " ");
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

check("1 /api/router/cache/sync remains", () => {
  mustContain("sync route", API, String.raw`"/api/router/cache/sync"`);
});

check("2 /api/router/cache/refresh remains", () => {
  mustContain("refresh route", API, String.raw`"/api/router/cache/refresh"`);
});

check("3 Sync uses Configuration mode", () => {
  mustContain("config collect", PLATFORM, "RouterCacheCollectMode::Configuration");
  mustContain("synchronizeRouterCache", WORKER, "synchronizeRouterCache\\(false\\)");
  mustContain("enum Configuration", IRH, "Configuration\\s*=\\s*0");
});

check("4 Refresh uses Telemetry mode", () => {
  mustContain("telemetry collect", PLATFORM, "RouterCacheCollectMode::Telemetry");
  mustContain("refreshRouterTelemetry", WORKER, "refreshRouterTelemetry\\(\\)");
  mustContain("enum Telemetry", IRH, "Telemetry\\s*=\\s*1");
  mustContain("AdminRefreshCache op", WORKER_H, "AdminRefreshCache");
  mustContain("enqueue refresh", WORKER, "enqueueAdminRefreshCache");
});

const snapshotBody = stripComments(
  extractFunction(
    DRIVER,
    "bool MikroTikDriver::collectCacheSnapshot(JsonDocument &out",
  ),
);

check("5 Sync path does not invoke /ip/route/print", () => {
  mustNotContain("no route print in snapshot", snapshotBody, String.raw`"/ip/route/print"`);
});

check("6 Sync path does not invoke observeAndRepairWan", () => {
  mustNotContain("no wan repair in snapshot", snapshotBody, String.raw`observeAndRepairWan\s*\(`);
});

check("7 Sync path does not invoke reconcileCaptiveHotspotPath", () => {
  mustNotContain(
    "no captive reconcile in snapshot",
    snapshotBody,
    String.raw`reconcileCaptiveHotspotPath\s*\(`,
  );
});

check("8 Refresh path does not invoke /ip/route/print", () => {
  mustNotContain("no route print", snapshotBody, String.raw`"/ip/route/print"`);
});

check("9 Refresh path does not invoke WAN repair", () => {
  mustNotContain("no wan repair", snapshotBody, String.raw`observeAndRepairWan\s*\(`);
});

check("10 Refresh path does not invoke captive reconciliation", () => {
  mustNotContain("no captive", snapshotBody, String.raw`reconcileCaptiveHotspotPath\s*\(`);
});

check("11 Refresh is read-only (no mutators in telemetry branch)", () => {
  mustContain("telemetry mode branch", snapshotBody, "RouterCacheCollectMode::Telemetry");
  mustNotContain("no set", snapshotBody, String.raw`"/ip/hotspot/set"`);
  mustNotContain("no add", snapshotBody, String.raw`"/ip/hotspot/add"`);
  mustNotContain("no route remove", snapshotBody, String.raw`"/ip/route/remove"`);
  mustNotContain("no dhcp set", snapshotBody, String.raw`"/ip/dhcp-client/set"`);
  mustNotContain("no bridge", snapshotBody, String.raw`"/interface/bridge`);
});

check("12 Worker architecture remains intact", () => {
  mustContain("same worker class", WORKER_H, "class RouterProvisioningWorker");
  mustContain("single queue", WORKER, "xQueueReceive\\(_queue");
  mustContain("AdminSyncCache retained", WORKER_H, "AdminSyncCache");
});

check("13 One RouterOS client/session per job remains", () => {
  mustContain("open once", snapshotBody, "openRouterSession");
  mustContain("close", snapshotBody, "closeRouterSession");
  // collectCacheSnapshot must not open a second independent client type
  mustNotContain("no second client", snapshotBody, "new RouterOsClient");
});

check("14 No /api/admin/sync exists", () => {
  mustNotContain("no admin sync", API, String.raw`/api/admin/sync`);
});

check("15 Login stale path uses Configuration sync", () => {
  mustContain("stale uses syncRouter", SYNC, "routerApi\\.syncRouter");
  mustContain("sync hits cache/sync", ROUTER_TS, String.raw`/cache/sync`);
  // ApiServer sync route must enqueue config sync, not refresh
  const syncRouteIdx = API.indexOf('"/api/router/cache/sync"');
  const syncSlice = API.slice(syncRouteIdx, syncRouteIdx + 500);
  mustContain("false for sync", syncSlice, String.raw`routerCacheEnqueue\(req,\s*false`);
  mustContain("config success message", syncSlice, "Router configuration synchronized");
});

check("16 Cache persistence remains worker-side", () => {
  mustContain("applyLiveSnapshot save", CACHE, "stampSynchronized");
  mustContain("save in applyLiveSnapshot", CACHE, "const bool ok = save\\(\\)");
  mustNotContain("no writeJson in ApiServer sync handler", API, String.raw`cache/sync[\s\S]{0,800}writeJson`);
});

check("17 Timeout unchanged at 20s", () => {
  mustContain("20s timeout", CFG, "ROUTER_WORKER_JOB_TIMEOUT_MS\\s*=\\s*20000");
});

check("18 Repair helpers retained in tree", () => {
  mustContain("observeAndRepairWan retained", DRIVER, "void MikroTikDriver::observeAndRepairWan");
  const wireless = read(path.join(ROOT, "src", "RouterWirelessAdapter.cpp"));
  mustContain("reconcile retained", wireless, "bool reconcileCaptiveHotspotPath");
});

check("19 Refresh route enqueues telemetry op", () => {
  const refreshIdx = API.indexOf('"/api/router/cache/refresh"');
  const refreshSlice = API.slice(refreshIdx, refreshIdx + 500);
  mustContain("refresh true", refreshSlice, "routerCacheEnqueue\\(req, true");
  mustContain("refresh job label", refreshSlice, "admin-refresh-cache");
});

check("20 ApiServer sync/refresh share worker not ROS", () => {
  mustContain("enqueue sync", API, "enqueueAdminSyncCache");
  mustContain("enqueue refresh", API, "enqueueAdminRefreshCache");
  mustNotContain("no ROS in ApiServer collect", API, "collectCacheSnapshot");
});

const failed = checks.filter((ok) => !ok).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} router sync/refresh contracts passed.`
    : `\n${failed}/${checks.length} contracts failed.`,
);
process.exit(failed === 0 ? 0 : 1);
