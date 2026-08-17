#!/usr/bin/env node
/**
 * Static contract check: session generation, stale cleanup, health Activate,
 * IP path, timer, portal Activating. Source-only — no RouterOS, no flash.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const REPO = path.resolve(ROOT, "..");

const read = (p) => fs.readFileSync(p, "utf8");
const PSM = read(path.join(ROOT, "src", "PortalSessionManager.cpp"));
const PSM_H = read(path.join(ROOT, "src", "PortalSessionManager.h"));
const WORKER_H = read(path.join(ROOT, "src", "RouterProvisioningWorker.h"));
const WORKER_C = read(path.join(ROOT, "src", "RouterProvisioningWorker.cpp"));
const GATE_C = read(path.join(ROOT, "src", "RouterApiTransportGate.cpp"));
const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const MODELS = read(path.join(ROOT, "src", "Models.h"));
const APP = read(path.join(REPO, "portal", "renzfi-app.js"));

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

check("1 sessionGeneration on HotspotUser and outcome", () => {
  mustContain("user gen", MODELS, "sessionGeneration");
  mustContain("outcome gen", WORKER_H, "uint32_t generation");
  mustContain("slot gen", WORKER_H, "hotspotGeneration");
});

check("2 stale outcome ignored", () => {
  mustContain("stale outcome", PSM, "stale outcome kind=");
  mustContain("stale expire", PSM, "stale job gen=");
  mustContain("live deauth ignored", PSM, "deauth outcome ignored live");
});

check("3 donePaying reserves without connected", () => {
  mustContain("activating", PSM, String.raw`session\["sessionState"\]\s*=\s*PortalState::Activating`);
  mustContain("connected false", PSM, String.raw`session\["connected"\]\s*=\s*false`);
  mustContain("bump gen", PSM, "bumpSessionGenerationUnlocked");
});

check("4 timer decrement requires connected", () => {
  mustContain(
    "tick connected",
    PSM,
    String.raw`isActive && !isPaused && \(session\["connected"\]`,
  );
});

check("5 coin window 0-credit expire requires hadRouterAuth", () => {
  mustContain("hadRouterAuth", PSM, String.raw`hadRouterAuth"\] \| false`);
});

check("6 Activate allowed during RECOVERING", () => {
  mustContain("recovering activate", GATE_C, "RouterHealth::Recovering");
  mustContain(
    "activate gate",
    GATE_C,
    String.raw`allowsHotspotActivate\(\) \{[\s\S]{0,400}?Recovering`,
  );
});

check("7 Critical success marks HEALTHY", () => {
  mustContain("customerCritical", GATE_C, "customerCritical");
});

check("8 skip probe when Activate can prove readiness", () => {
  mustContain("activateCanProve", PSM, "activateCanProve");
  mustContain("hasCustomerActivatePending", PSM, "hasCustomerActivatePending");
});

check("9 done-paying accepts IP", () => {
  mustContain("body ip", API, String.raw`body\["ip"\]`);
  mustContain("donePaying ip", PSM_H, "donePaying");
  mustNotContain("no hardcoded .251", API, String.raw`10\.20\.0\.251`);
  mustNotContain("no hardcoded .254", API, String.raw`10\.20\.0\.254`);
  mustNotContain("no hardcoded .251 psm", PSM, String.raw`10\.20\.0\.251`);
  mustNotContain("no hardcoded .254 psm", PSM, String.raw`10\.20\.0\.254`);
});

check("10 portal no 35s terminal failure", () => {
  mustNotContain("no 35000 fail", APP, "waitForActivation\\(35000\\)");
  mustContain("state-driven wait", APP, "Still connecting to the router");
  mustContain("generation guard", APP, "incomingGen < currentGen");
});

check("11 mailbox preserves newer generation", () => {
  mustContain("keep discarded", WORKER_C, "keepDiscarded");
});

check("12 no idle ROS poll added", () => {
  mustNotContain("no 1s identity", PSM, "identity/print");
  mustContain("idle log", PSM, "idle no-router-work");
});

check("13 drain prefers Activate over leftover cleanup", () => {
  mustContain("livePurchase", PSM, "livePurchase");
  mustContain("clear superseded", PSM, "clearSupersededCleanupUnlocked");
});

check("14 terminate tags Expire with generation", () => {
  mustContain("terminateGen", PSM, "terminateGen");
});

check("15 single worker / no parallel session", () => {
  mustContain("single slot", WORKER_C, "enqueueFireAndForget");
  mustNotContain("no second worker create", WORKER_C, "xTaskCreate.*router_worker_2");
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} session-sync contracts PASS`
    : `\n${failed}/${checks.length} session-sync contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
