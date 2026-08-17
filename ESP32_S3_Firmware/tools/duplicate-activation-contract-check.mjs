#!/usr/bin/env node
/**
 * Static contract: one Done Paying → one Activate; no active/set limit-uptime;
 * RouterOS TRAP cannot become ok=yes. Source-only.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const REPO = path.resolve(ROOT, "..");

const read = (p) => fs.readFileSync(p, "utf8");
const PSM = read(path.join(ROOT, "src", "PortalSessionManager.cpp"));
const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const DRIVER = read(path.join(ROOT, "src", "router", "drivers", "MikroTikDriver.cpp"));
const CLIENT = read(path.join(ROOT, "src", "RouterOsClient.cpp"));
const WORKER_C = read(path.join(ROOT, "src", "RouterProvisioningWorker.cpp"));
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

check("1 One Done Paying → one ActivateHotspotUser path", () => {
  mustContain("direct then deferred only if fail", PSM,
    String.raw`routerAccepted = onSessionActivated\(mac\)`);
  mustContain("clear pending after deferred enqueue", PSM,
    String.raw`queued\["activationRetryPending"\] = false`);
});

check("2 Same generation cannot activate twice", () => {
  mustContain("already authorized helper", PSM, "alreadyAuthorizedThisGeneration");
  mustContain("skip duplicate log", PSM, "already authorized — skip duplicate");
});

check("3 Connected generation cannot enqueue Activate again", () => {
  mustContain("enqueue guard", PSM, "already authorized — not queued");
  mustContain("idle retry skip", PSM,
    String.raw`alreadyAuthorizedThisGeneration\(session\)`);
});

check("4 GET /session cannot enqueue activation", () => {
  mustNotContain("no activate in GET session", API, "tryEnqueueActivateHotspotUser");
  mustNotContain("no onSessionActivated in api", API, "onSessionActivated");
});

check("5 SSE cannot enqueue activation", () => {
  mustNotContain("no activate in portal js", APP, "tryEnqueueActivateHotspotUser");
  mustNotContain("no activate-hotspot in portal js", APP, "activate-hotspot-user");
});

check("6 secondsLeft decrement cannot enqueue activation", () => {
  mustNotContain("no activate in tick decrement", PSM,
    String.raw`secondsLeft"\] = secs - 1[\s\S]{0,200}ActivateSession`);
});

check("7 RouterOS Active verification cannot enqueue activation", () => {
  mustContain("verify only VerifyActive", WORKER_C, "VerifyHotspotActive");
  mustNotContain("verify does not activate", WORKER_C,
    String.raw`tryEnqueueVerifyHotspotActive[\s\S]{0,400}tryEnqueueActivateHotspotUser`);
});

check("8 RouterOS active/set TRAP path removed / cannot succeed", () => {
  mustNotContain("no active/set execute", DRIVER,
    String.raw`executeCommand\("/ip/hotspot/active/set"`);
  mustContain("active present without set", DRIVER, "no active/set");
});

check("9 RouterOS trap cannot produce worker ok=yes", () => {
  mustContain("client trap returns false", CLIENT, String.raw`if \(out->trapReceived\)`);
  mustContain("client trap return false", CLIENT, "return false;");
  mustNotContain("no void execute on hotspot set", DRIVER,
    String.raw`\(void\)_routerOs\.executeCommand\("/ip/hotspot/active`);
  mustContain("login trap fails", DRIVER, String.raw`!loginResult\.trapReceived`);
});

check("10 Fresh user activation still uses User Model B", () => {
  mustContain("model B add", DRIVER, "newLimitSeconds \\+= requestedSeconds");
  mustContain("existing uptime", DRIVER, "existing_uptime=");
});

check("11 Add Time does not create a second authorization clock", () => {
  mustContain("active present reuse", DRIVER, "operation=active_present");
  mustNotContain("no active/set extend", DRIVER, "operation=active_set");
});

check("12 No extra RouterOS polling is introduced", () => {
  mustContain("verify still 60s", PSM, "kVerifyIntervalMs = 60000");
  mustNotContain("no 1s active print in psm", PSM, "active/print");
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} duplicate-activation contracts PASS`
    : `\n${failed}/${checks.length} duplicate-activation contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
