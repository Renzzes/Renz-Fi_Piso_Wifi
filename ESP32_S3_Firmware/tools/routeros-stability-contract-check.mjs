#!/usr/bin/env node
/**
 * Static contract check: RouterOS health FSM + idle/verify/retry suppression.
 * Source-only — does not execute RouterOS or flash hardware.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");

const read = (p) => fs.readFileSync(p, "utf8");
const GATE_H = read(path.join(ROOT, "src", "RouterApiTransportGate.h"));
const GATE_C = read(path.join(ROOT, "src", "RouterApiTransportGate.cpp"));
const WORKER_C = read(path.join(ROOT, "src", "RouterProvisioningWorker.cpp"));
const PSM = read(path.join(ROOT, "src", "PortalSessionManager.cpp"));
const CFG = read(path.join(ROOT, "src", "Config.h"));
const DRIVER = read(
  path.join(ROOT, "src", "router", "drivers", "MikroTikDriver.cpp"),
);

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

check("1 health FSM enum present", () => {
  mustContain("Recovering", GATE_H, "Recovering");
  mustContain("Unavailable", GATE_H, "Unavailable");
  mustContain("Probing", GATE_H, "Probing");
  mustContain("Cooldown", GATE_H, "Cooldown");
});

check("2 health config constants", () => {
  mustContain("fails", CFG, "ROUTER_HEALTH_FAILS_TO_UNAVAILABLE");
  mustContain("dwell", CFG, "ROUTER_HEALTH_RECOVERY_DWELL_MS");
  mustContain("probe interval", CFG, "ROUTER_HEALTH_PROBE_MIN_INTERVAL_MS");
  mustContain("trust", CFG, "ROUTER_ACTIVATE_TRUST_WINDOW_MS");
});

check("3 Verify demoted from Critical", () => {
  mustContain(
    "Verify Normal",
    WORKER_C,
    String.raw`verify-hotspot-active priority=normal`,
  );
  mustNotContain(
    "Verify not Critical priority assign",
    WORKER_C,
    String.raw`VerifyHotspotActive[\s\S]{0,200}?RouterJobPriority::Critical`,
  );
  mustContain(
    "Critical only activate/deauth/pause",
    WORKER_C,
    String.raw`ActivateHotspotUser \|\|[\s\S]{0,80}?DeauthorizeHotspotUser \|\|[\s\S]{0,80}?PauseHotspotUser`,
  );
});

check("4 Verify gated by allowsHotspotVerify", () => {
  mustContain(
    "verify gate",
    WORKER_C,
    String.raw`tryEnqueueVerifyHotspotActive[\s\S]{0,200}?allowsHotspotVerify`,
  );
  mustContain(
    "verify Healthy only",
    GATE_C,
    String.raw`allowsHotspotVerify\(\) \{[\s\S]{0,80}?Healthy`,
  );
});

check("5 Activate gated by health and ethernet", () => {
  mustContain(
    "ethernet gate before health",
    WORKER_C,
    String.raw`tryEnqueueActivateHotspotUser[\s\S]{0,900}?ethernetReadyForHotspot[\s\S]{0,400}?allowsHotspotActivate`,
  );
  mustContain(
    "activate deferred log",
    PSM,
    "activate deferred reason=router_unavailable",
  );
  mustContain(
    "ethernet not ready log",
    WORKER_C,
    "ethernet_not_ready",
  );
});

check("6 idle no-router-work log + needsRouterOsWork", () => {
  mustContain("idle log", PSM, String.raw`idle no-router-work`);
  mustContain("needsRouterOsWork", PSM, "needsRouterOsWork");
});

check("7 health probe only for critical recovery work", () => {
  mustContain(
    "probe + recovery work",
    PSM,
    String.raw`wantsHealthProbe\(now\) &&[\s\S]{0,80}?needsHealthRecoveryProbe\(\)`,
  );
  mustNotContain(
    "Connected-only must not gate HealthProbe",
    PSM,
    String.raw`wantsHealthProbe\(now\) && needsRouterOsWork\(\)`,
  );
  mustContain("probeApiReady", DRIVER, "probeApiReady");
  mustContain("identity print", DRIVER, String.raw`/system/identity/print`);
  mustContain(
    "login timeout suppress log",
    PSM,
    "verify-login-timeout connected=1 probe_suppressed=1",
  );
});

check("8 post-activation trust window", () => {
  mustContain(
    "trust window",
    PSM,
    "ROUTER_ACTIVATE_TRUST_WINDOW_MS",
  );
  mustContain("_lastActivateSuccessMs", PSM, "_lastActivateSuccessMs");
});

check("9 recovery drain priority cleanup first", () => {
  mustContain(
    "drain cleanup log",
    PSM,
    String.raw`recovery drain cleanup mac=`,
  );
  mustContain(
    "drain activate log",
    PSM,
    String.raw`recovery drain activate mac=`,
  );
});

check("10 admin deferred only during storage recovery", () => {
  mustContain(
    "storage recovery gate",
    WORKER_C,
    "storageRecoveryBlocksAdmin",
  );
  mustContain(
    "admin deferred log",
    WORKER_C,
    "admin job deferred reason=storage_recovery",
  );
  mustContain(
    "recovery 503 code",
    WORKER_C,
    "ROUTER_RECOVERY_IN_PROGRESS",
  );
  mustNotContain(
    "admin enqueue not gated on ROS health",
    WORKER_C,
    String.raw`enqueueAdminRefreshCache[\s\S]{0,400}?allowsAdminNonEssential`,
  );
});

check("11 portal session/heartbeat remain local", () => {
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

check("12 single HealthProbe op type", () => {
  mustContain("HealthProbe enqueue", WORKER_C, "tryEnqueueHealthProbe");
  mustContain("HealthProbe run", WORKER_C, "OpType::HealthProbe");
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} RouterOS stability contracts PASS`
    : `\n${failed}/${checks.length} RouterOS stability contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
