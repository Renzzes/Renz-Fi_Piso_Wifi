#!/usr/bin/env node
/**
 * Static contract: async_tcp / ESPAsyncWebServer hot paths must not tip TWDT
 * via durable Logger::info/warn/error → appendHistory → flush.
 *
 * Context-correct: worker/loopTask durable logs are allowed; HTTP-reachable
 * auth/portal/admin tip sites must use *Local variants.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const src = (...p) => path.join(ROOT, "src", ...p);
const read = (p) => fs.readFileSync(p, "utf8");

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

function mustContain(text, pattern, label) {
  if (!new RegExp(pattern, "ms").test(text)) {
    throw new Error(`missing ${label}: ${pattern}`);
  }
}

function mustNotContain(text, pattern, label) {
  if (new RegExp(pattern, "ms").test(text)) {
    throw new Error(`forbidden ${label}: ${pattern}`);
  }
}

const LOGGER = read(src("Logger.h"));
const LOGGER_CPP = read(src("Logger.cpp"));
const AUTH = read(src("AuthManager.cpp"));
const API = read(src("ApiServer.cpp"));
const PORTAL = read(src("PortalSessionManager.cpp"));
const STORAGE = read(src("StorageManager.cpp"));
const PROMO = read(src("PromoManager.cpp"));
const VOUCHER = read(src("VoucherManager.cpp"));
const SALES = read(src("SessionManager.cpp"));
const COIN = read(src("CoinManager.cpp"));

check("1 Logger exposes infoLocal/warnLocal/errorLocal", () => {
  mustContain(LOGGER, String.raw`void infoLocal\(`, "infoLocal");
  mustContain(LOGGER, String.raw`void warnLocal\(`, "warnLocal");
  mustContain(LOGGER, String.raw`void errorLocal\(`, "errorLocal");
  mustContain(LOGGER_CPP, String.raw`write\(LogLevel::Info, type, message, false\)`, "infoLocal non-durable");
  mustContain(LOGGER_CPP, String.raw`write\(LogLevel::Warn, type, message, false\)`, "warnLocal non-durable");
  mustContain(LOGGER_CPP, String.raw`write\(LogLevel::Error, type, message, false\)`, "errorLocal non-durable");
  mustContain(LOGGER_CPP, String.raw`if \(!durableHistory\) return;`, "history gate");
});

check("2 Auth login path is Local-only for tip logs", () => {
  const login = AUTH.slice(AUTH.indexOf("bool AuthManager::login"), AUTH.indexOf("bool AuthManager::login") + 2800);
  mustContain(login, String.raw`warnLocal\("auth", "Failed login"\)`, "failed login");
  mustContain(login, String.raw`errorLocal\("auth", "Failed to create admin session"\)`, "session fail");
  mustContain(login, String.raw`infoLocal\("auth", "Login successful"\)`, "success");
  mustNotContain(login, String.raw`->info\("auth", "Login successful"\)`, "no durable success");
  mustNotContain(login, String.raw`->warn\("auth", "Failed login"\)`, "no durable fail");
});

check("3 Auth HTTP mutations use infoLocal", () => {
  mustContain(AUTH, String.raw`infoLocal\("auth", "Admin password changed"\)`, "change password");
  mustContain(AUTH, String.raw`infoLocal\("auth",[\s\S]*operator persistence ok`, "operator");
  mustContain(AUTH, String.raw`infoLocal\("auth", "Owner credentials provisioned"\)`, "owner");
});

check("4 ApiServer HTTP tip logs use Local variants", () => {
  mustNotContain(API, String.raw`_logger->info\(`, "no durable info in ApiServer");
  mustNotContain(API, String.raw`_logger->warn\(`, "no durable warn in ApiServer");
  mustNotContain(API, String.raw`_logger->error\(`, "no durable error in ApiServer");
  mustContain(API, String.raw`infoLocal\("backup"`, "backup local");
  mustContain(API, String.raw`infoLocal\("restore"`, "restore local");
  mustContain(API, String.raw`infoLocal\("firmware"`, "firmware local");
});

check("5 Portal HTTP tip paths use Local logging", () => {
  mustContain(PORTAL, String.raw`infoLocal\("portal", "Coin window opened`, "coin window");
  mustContain(PORTAL, String.raw`infoLocal\("portal", "Session terminated by user request`, "terminate");
  mustContain(PORTAL, String.raw`infoLocal\("portal",[\s\S]*Session activating`, "donePaying success");
  mustContain(PORTAL, String.raw`errorLocal\("portal",[\s\S]*activation queue full`, "queue full");
  mustContain(PORTAL, String.raw`infoLocal\("portal", "RouterOS hotspot user activation queued`, "activate queued");
});

check("6 Coin pulse path keeps durable Logger (loopTask)", () => {
  const start = PORTAL.indexOf("void PortalSessionManager::onCoinInserted");
  const end = PORTAL.indexOf("bool PortalSessionManager::donePaying");
  const body = PORTAL.slice(start, end > start ? end : start + 15000);
  mustContain(body, String.raw`Coin pulse received but no active coin window`, "pulse warn");
  mustContain(body, String.raw`_logger->warn\(`, "pulse uses durable warn");
  mustContain(body, String.raw`Credit \+PHP`, "credit message");
  mustContain(body, String.raw`_logger->info\(`, "credit durable info");
});

check("7 Admin CRUD tip logs are Local", () => {
  mustContain(PROMO, String.raw`infoLocal\("promos", "Promo created"\)`, "promo");
  mustContain(VOUCHER, String.raw`infoLocal\("vouchers", "Vouchers generated"\)`, "voucher");
  mustContain(SALES, String.raw`infoLocal\("sales", "export generated"\)`, "sales export");
  mustContain(SALES, String.raw`infoLocal\("coin", "Coin session granted"\)`, "coin test");
  mustContain(COIN, String.raw`infoLocal\("coin", "Coin diagnostics counters reset"\)`, "coin reset");
});

check("8 Status/health use snapshots; heavy walks throttled", () => {
  mustContain(STORAGE, "STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS", "throttle");
  const fn = STORAGE.slice(STORAGE.indexOf("void StorageManager::refreshRuntimeSnapshot"));
  const heavy = fn.slice(fn.indexOf("STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS"), fn.indexOf("STORAGE_SNAPSHOT_HEAVY_INTERVAL_MS") + 2200);
  mustContain(heavy, "fallbackTotalBytes", "heavy inside throttle");
  mustContain(STORAGE, String.raw`spiffs\["fallbackBytes"\] = _snapshotEmergencyBytes`, "no live scan in fillStorageHealth");
});

check("9 Router admin mutations remain 202 worker enqueue", () => {
  mustContain(API, "enqueueAdminSaveSettings", "save settings worker");
  mustContain(API, "enqueueAdminTest", "test worker");
  mustContain(API, "enqueueAdminSyncCache", "cache sync worker");
  mustContain(API, "enqueueAdminSaveWireless", "wireless worker");
  mustNotContain(API, String.raw`/api/admin/sync`, "no admin sync route");
});

check("10 Factory reset remains worker (not sync HTTP)", () => {
  mustContain(API, "FactoryResetWorker", "factory reset type");
  mustContain(API, String.raw`/api/system/factory-reset`, "factory reset route");
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} async_tcp blocking-audit contracts PASS`
    : `\n${failed}/${checks.length} async_tcp blocking-audit contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
