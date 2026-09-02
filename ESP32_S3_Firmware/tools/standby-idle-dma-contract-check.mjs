#!/usr/bin/env node
/**
 * Contract: idle portal + thin Admin Connect stay off continuous ETH polling
 * (DMA / W5500 Guru prevention).
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const REPO = path.resolve(ROOT, "..");
const read = (p) => fs.readFileSync(p, "utf8");

const PORTAL = read(path.join(REPO, "portal", "renzfi-app.js"));
const SYNC = read(path.join(REPO, "src", "services", "adminSync.ts"));
const DASH = read(path.join(REPO, "src", "pages", "DashboardPage.tsx"));
const APP = read(path.join(REPO, "src", "App.tsx"));
const MONITOR = read(path.join(REPO, "src", "hooks", "useAdminApiMonitor.ts"));
const EVENTS = read(path.join(REPO, "src", "hooks", "useDashboardEvents.ts"));
const LIFE_H = read(path.join(ROOT, "src", "ManagementApLifecycle.h"));
const LIFE_C = read(path.join(ROOT, "src", "ManagementApLifecycle.cpp"));
const FW = read(path.join(ROOT, "src", "FirmwareApp.cpp"));

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

function must(label, text, re) {
  if (!new RegExp(re, "ms").test(text)) throw new Error(`missing: ${label}`);
}
function mustNot(label, text, re) {
  if (new RegExp(re, "ms").test(text)) throw new Error(`forbidden: ${label}`);
}

check("1 Portal idle standby policy exists", () => {
  must("updateIdleSyncPolicy", PORTAL, "function updateIdleSyncPolicy");
  must("ACTIVE_SYNC_MS", PORTAL, "ACTIVE_SYNC_MS\\s*=\\s*60000");
  mustNot("no HEARTBEAT_MS 10s loop", PORTAL, "HEARTBEAT_MS\\s*=\\s*10000");
  mustNot("init must not startHeartbeat()", PORTAL, "startHeartbeat\\(\\)");
});

check("2 Portal init does not force EventSource", () => {
  // After bindEvents, policy decides — not unconditional connectPortalEvents().
  const initIdx = PORTAL.indexOf("function init()");
  const initBody = PORTAL.slice(initIdx, initIdx + 3500);
  must("calls updateIdleSyncPolicy in init path", initBody, "updateIdleSyncPolicy");
  mustNot(
    "no unconditional connectPortalEvents in init finally",
    initBody,
    "bindEvents\\(\\);\\s*connectPortalEvents\\(\\)",
  );
});

check("3 Admin Connect has no routerApi.syncRouter", () => {
  mustNot("no syncRouter", SYNC, "routerApi\\.syncRouter");
  must("status only", SYNC, "systemApi\\.status");
});

check("4 Dashboard Reload Sales + Synchronize Router + live toggle", () => {
  must("Reload Sales", DASH, "Reload Sales");
  must("Synchronize Router button", DASH, "Synchronize Router");
  must("live updates toggle", DASH, "Live updates");
  must("chart gated", DASH, "salesChartEnabled");
  must("details gated", DASH, "detailsEnabled");
});

check("5 Admin SSE opt-in via liveUpdatesEnabled", () => {
  must("liveUpdatesEnabled gate", APP, "liveUpdatesEnabled");
  must("useDashboardEvents gated", APP, "liveUpdatesEnabled");
  must("standbyIdle health", MONITOR, "standbyIdle");
});

check("6 SoftAP production idle yield", () => {
  must("notifyProductionPlaneReady decl", LIFE_H, "notifyProductionPlaneReady");
  must("processProductionSoftApYield", LIFE_C, "processProductionSoftApYield");
  must("FirmwareApp notifies", FW, "notifyProductionPlaneReady\\(\\)");
  must("idle yield constant", LIFE_C, "kProductionSoftApIdleYieldMs\\s*=\\s*2000");
  must(
    "post-yield SoftAP restart allowed",
    LIFE_C,
    "production ETH plane is live and SoftAP was idle-yielded",
  );
  must("notify attempts immediate yield", LIFE_C, "processProductionSoftApYield\\(\\)");
});

check("7 EventBus hook still no-ops without clients", () => {
  must("fallback poll helper", EVENTS, "fallbackPollMs");
});

check("8 System Config section-gates heavy queries", () => {
  const page = read(path.join(REPO, "src", "pages", "SystemConfigurationPage.tsx"));
  must("hotspot gates settings", page, "enabled: sectionHotspot");
  must("wireless gated", page, "enabled: sectionWireless");
  must("network gated", page, "enabled: sectionNetwork");
  must("no cacheFetching on Sync disable", page, "Do not gate Sync/Refresh on cacheFetching");
  mustNot("no 30s health poll on System Config", page, "refetchInterval:\\s*30_000");
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} standby-idle DMA contracts PASS`
    : `\n${failed}/${checks.length} standby-idle DMA contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
