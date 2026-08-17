#!/usr/bin/env node
/**
 * Static contract check: one authorization event, one expiry timeline,
 * presentation-only browser timer, no idle RouterOS polling.
 * Source-only — no RouterOS, no flash.
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
const DRIVER = read(path.join(ROOT, "src", "router", "drivers", "MikroTikDriver.cpp"));
const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const MODELS = read(path.join(ROOT, "src", "Models.h"));
const APP = read(path.join(REPO, "portal", "renzfi-app.js"));
const MAIN = fs.existsSync(path.join(ROOT, "src", "main.cpp"))
  ? read(path.join(ROOT, "src", "main.cpp"))
  : "";

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

check("1 Activating cannot run timer", () => {
  mustContain(
    "timer requires connected",
    PSM,
    String.raw`timerRunning.*=.*Active[\s\S]{0,200}?connected`,
  );
  mustContain(
    "donePaying connected false",
    PSM,
    String.raw`session\["connected"\]\s*=\s*false`,
  );
  mustContain("expires cleared at reserve", PSM, String.raw`session\["expiresAtMs"\]\s*=\s*0U`);
});

check("2 Connected requires matching RouterOS success", () => {
  mustContain("hadRouterAuth on success", PSM, String.raw`session\["hadRouterAuth"\]\s*=\s*true`);
  mustContain("expected activating", PSM, "routerAuthPending");
  mustContain("stale outcome ignored", PSM, "stale outcome kind=");
});

check("3 RouterOS authorization produces authoritative session start", () => {
  mustContain("ActivateAuthTrace", MODELS, "struct ActivateAuthTrace");
  mustContain("authorizedAtMs outcome", WORKER_H, "authorizedAtMs");
  mustContain("commitAuthorizedClock", PSM, "commitAuthorizedClockUnlocked");
  mustContain("authorizedAt from outcome", PSM, "outcome.authorizedAtMs");
});

check("4 GET cannot increase remaining time", () => {
  mustContain("serverRemainingOf", APP, "function serverRemainingOf");
  mustContain("applySessionClock", APP, "function applySessionClock");
  mustContain("never later unless increase", APP, "legitimateIncrease");
  mustNotContain(
    "no trustFully rebase",
    APP,
    String.raw`anchorSession\(session\.secondsLeft,\s*session\.timerRunning,\s*trustFully\)`,
  );
});

check("5 stale GET cannot overwrite newer generation", () => {
  mustContain("gen guard", APP, "incomingGen < currentGen");
  mustContain("sessionSyncGen", APP, "result.gen !== sessionSyncGen");
});

check("6 stale SSE cannot overwrite newer generation", () => {
  mustContain("sse applyNormalized", APP, String.raw`applyNormalizedSession\(session`);
  mustContain("sse gen guard same path", APP, "incomingGen < currentGen");
});

check("7 re-entry cannot reset timer", () => {
  mustContain(
    "waitForActivation no trustFully",
    APP,
    String.raw`applyFetchedSession\(result, false\)`,
  );
  mustContain("no later from GET", APP, "candidate < sessionExpiryAt");
});

check("8 browser cannot become timer authority", () => {
  mustContain("presentation only", APP, "Presentation only");
  mustContain("serverNowMs published", PSM, String.raw`out\["serverNowMs"\]`);
  mustContain("expiresAtMs published", PSM, String.raw`out\["expiresAtMs"\]`);
});

check("9 Active missing after confirmed verification causes authorization loss", () => {
  mustContain("not_active", PSM, "not_active");
  mustContain("activation_error on miss", PSM, "Internet authorization lost");
  mustContain("freeze on loss", PSM, "freezeSessionClockUnlocked");
});

check("10 RouterOS transport failure preserves entitlement", () => {
  // Verify transport/login failure must leave Connected + secondsLeft intact.
  // 2026-08-15 stability: still `continue` without mutating the session; a
  // HealthProbe-suppression diagnostic is logged first.
  mustContain(
    "verify query fail continue",
    PSM,
    String.raw`if \(!outcome\.ok\) \{[\s\S]{0,300}?continue`,
  );
  mustContain("secondsLeft preserved log", PSM, "secondsLeft preserved");
  mustContain("no Connected-only probe after verify fail", PSM, "probe_suppressed=1");
});

check("11 Add Time is the only legitimate timer increase during active session", () => {
  mustContain("addTime remaining", PSM, "existingRemaining \\+ purchasedSeconds");
  mustContain("portal increase only gen/granted", APP, "incomingGranted > \\(Number\\(previousGranted\\)");
});

check("12 new purchase starts new generation", () => {
  mustContain("bump on new purchase", PSM, "bumpSessionGenerationUnlocked");
  mustContain("addTime skips bump", PSM, String.raw`if \(!addTime\)`);
});

check("13 old cleanup cannot affect new generation", () => {
  mustContain("stale expire", PSM, "stale job gen=");
  mustContain("live deauth ignored", PSM, "deauth outcome ignored live");
  mustContain("owner reset tags gen", PSM, "resetGen");
});

check("14 idle produces zero RouterOS API work", () => {
  mustContain("idle log", PSM, "idle no-router-work");
  mustContain("needsRouterOsWork", PSM_H, "needsRouterOsWork");
  mustNotContain("no identity poll in portal", PSM, "identity/print");
});

check("15 no new RouterOS worker/task is introduced", () => {
  mustContain("single enqueue", WORKER_C, "enqueueFireAndForget");
  mustNotContain("no second worker", WORKER_C, "xTaskCreate.*router_worker_2");
  mustNotContain("no extra task in psm", PSM, "xTaskCreate");
});

check("16 no RouterOS calls from HTTP callbacks", () => {
  mustNotContain("no executeCommand in api", API, "executeCommand");
  mustNotContain("no provisionHotspot in api", API, "provisionHotspotUser");
});

check("17 no RouterOS calls from coin ISR", () => {
  const coin = read(path.join(ROOT, "src", "CoinManager.cpp"));
  mustNotContain("no executeCommand in coin", coin, "executeCommand");
  mustNotContain("no provision in coin", coin, "provisionHotspotUser");
});

check("18 timer is monotonic", () => {
  mustContain("clock remaining derive", PSM, "remainingFromExpiresMs");
  mustContain("browser never later", APP, "else if \\(candidate < sessionExpiryAt\\)");
});

check("19 expiry is deterministic", () => {
  mustContain("expiresAtMs = authorized + granted", PSM, "authorizedAtMs \\+ grantedSeconds \\* 1000");
  mustContain("user Model B", DRIVER, "newLimitSeconds \\+= requestedSeconds");
  mustNotContain(
    "no active/set limit-uptime",
    DRIVER,
    String.raw`executeCommand\("/ip/hotspot/active/set"`,
  );
});

check("20 Router reboot does not create a polling storm", () => {
  mustContain("verify coalesce", PSM, "maybeEnqueueActiveVerify");
  mustContain("health probe gated", PSM, "wantsHealthProbe");
  mustNotContain("no 1s active print", PSM, "active/print");
});

check("21 pause/resume share one clock and no dwell", () => {
  mustContain("pause freeze clock", PSM, "freezeSessionClockUnlocked");
  mustContain("owner pause same method", API, String.raw`pause\(mac, nullptr, false\)`);
  mustContain("owner resume same method", API, String.raw`_portalSessions->resume\(mac\)`);
  mustNotContain("no pause sleep", PSM, String.raw`pause[\s\S]{0,80}delay\(`);
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} session-clock contracts PASS`
    : `\n${failed}/${checks.length} session-clock contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
