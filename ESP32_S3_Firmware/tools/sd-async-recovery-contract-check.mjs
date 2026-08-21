#!/usr/bin/env node
/**
 * SD hot-unplug: remount/recovery must not run inside AsyncTCP callbacks.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const read = (p) => fs.readFileSync(p, "utf8");

const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const STORAGE = read(path.join(ROOT, "src", "StorageManager.cpp"));
const STORAGE_H = read(path.join(ROOT, "src", "StorageManager.h"));
const WORKER = read(path.join(ROOT, "src", "RouterProvisioningWorker.cpp"));
const GATE = read(path.join(ROOT, "src", "RouterApiTransportGate.cpp"));
const FWAPP = read(path.join(ROOT, "src", "FirmwareApp.cpp"));
const ENGINE = read(path.join(ROOT, "src", "RouterProvisioningEngine.cpp"));
const DRIVER = read(path.join(ROOT, "src", "router", "drivers", "MikroTikDriver.cpp"));

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

function extractAfter(src, needle, length) {
  const idx = src.indexOf(needle);
  if (idx < 0) throw new Error(`not found: ${needle}`);
  return src.slice(idx, idx + length);
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

const statusHandler = extractAfter(API, '"/api/storage/status"', 900);

check("1 /api/storage/status remains", () => {
  mustContain("status route", API, String.raw`"/api/storage/status"`);
});

check("2 /api/storage/status does not call SD.begin", () => {
  mustNotContain("no SD.begin in status handler", statusHandler, String.raw`SD\.begin\s*\(`);
});

check("3 /api/storage/status does not call SD.end", () => {
  mustNotContain("no SD.end in status handler", statusHandler, String.raw`SD\.end\s*\(`);
});

check("4 /api/storage/status does not remount", () => {
  mustNotContain("no attemptSdRecovery", statusHandler, "attemptSdRecovery");
  mustNotContain("no mountSdCard", statusHandler, "mountSdCard");
  mustNotContain("no retrySd execute", statusHandler, "retrySd");
  mustContain("snapshot fill", statusHandler, "fillStorageStatus");
});

check("5 ApiServer does not invoke SD recovery", () => {
  mustNotContain("no attemptSdRecovery in ApiServer", API, "attemptSdRecovery");
  mustNotContain("no mountSdCard in ApiServer", API, "mountSdCard");
  mustNotContain("no SD.begin in ApiServer", API, String.raw`SD\.begin\s*\(`);
});

check("6 Recovery is executed by storage owner/main loop", () => {
  mustContain("poll owner", FWAPP, String.raw`_storage\.pollStorageHealth\(`);
  mustContain("poll calls attemptSdRecovery", STORAGE, String.raw`void StorageManager::pollStorageHealth[\s\S]{0,2500}?attemptSdRecovery\(`);
  mustContain("in-flight guard", STORAGE_H, "_sdRecoveryInProgress");
  mustContain("single-flight check", STORAGE, String.raw`if \(_sdRecoveryInProgress\) return`);
});

check("7 Only one recovery can be active", () => {
  mustContain("request skips in-flight", STORAGE, "already in flight");
  mustContain("retrySd requests only", STORAGE, String.raw`bool StorageManager::retrySd[\s\S]{0,400}?requestSdRecovery\("api"\)`);
  mustNotContain("retrySd must not remount", STORAGE, String.raw`bool StorageManager::retrySd[\s\S]{0,500}?attemptSdRecovery`);
});

check("8 SD_READY releases router recovery gate", () => {
  mustContain(
    "Ready not blocked",
    WORKER,
    String.raw`storageRecoveryBlocksAdmin[\s\S]{0,1200}?default:\s*\n\s*return false`,
  );
  mustContain("gate log ready", WORKER, "reason=storage_ready");
});

check("9 Mounting/Remounting/Syncing block Admin router jobs; Degraded does not", () => {
  mustContain("Mounting", WORKER, "SdLifecycle::Mounting");
  mustContain("Remounting", WORKER, "SdLifecycle::Remounting");
  mustContain("Syncing", WORKER, "SdLifecycle::Syncing");
  // Steady SD_DEGRADED + SPIFFS fallback must keep Admin RouterOS usable
  // (operational continuity). Active remount/sync still blocks above.
  mustContain(
    "Degraded does not block",
    WORKER,
    String.raw`SdLifecycle::Degraded:\s*\n\s*default:\s*\n\s*return false`,
  );
  mustContain("deferred log", WORKER, "admin job deferred reason=storage_recovery");
});

check("10 RouterOS health is not forced HEALTHY by SD_READY", () => {
  mustNotContain("no SD_READY in ROS gate", GATE, "SD_READY");
  mustNotContain("no sdLifecycle in ROS success", GATE, "sdLifecycle");
});

check("11 Router credentials remain unchanged", () => {
  mustContain("ensure re-reads disk", ENGINE, "_productionCredentialsOk = false");
  mustContain("MikroTik reads ROUTER_FILE", DRIVER, "RenzFiConfig::ROUTER_FILE");
  mustContain("no hardcoded admin username", DRIVER, "RouterOS API username is not configured");
});

check("12 /api/status uses cached dashboard snapshot", () => {
  const statusApi = extractAfter(API, '"/api/status"', 12000);
  mustContain("fillDashboardStatus", statusApi, "fillDashboardStatus");
  mustNotContain("no fileSizeBytes on status", statusApi, "fileSizeBytes");
  mustNotContain("no SD.exists on status", statusApi, String.raw`SD\.exists`);
  mustNotContain("no SD.open on status", statusApi, String.raw`SD\.open`);
  mustContain("snapshot helper is RAM-only", STORAGE, String.raw`void StorageManager::fillDashboardStatus[\s\S]{0,400}?_dashSnap`);
});

check("13 HTTP route names remain unchanged", () => {
  mustContain("storage status", API, String.raw`"/api/storage/status"`);
  mustContain("retry-sd", API, String.raw`"/api/storage/retry-sd"`);
  mustContain("api status", API, String.raw`"/api/status"`);
  mustContain("api health", API, String.raw`"/api/health"`);
  mustContain("cache refresh", API, String.raw`"/api/router/cache/refresh"`);
  mustContain("cache sync", API, String.raw`"/api/router/cache/sync"`);
});

check("14 lock wait is zero during recovery", () => {
  mustContain("zero wait", STORAGE, String.raw`if \(_sdRecoveryInProgress\) wait = 0`);
});

const failed = checks.filter((ok) => !ok).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} SD async recovery contracts passed.`
    : `\n${failed}/${checks.length} SD async recovery contracts failed.`,
);
process.exit(failed === 0 ? 0 : 1);
