#!/usr/bin/env node
/**
 * Static contract check: absolute voucher expiry + profile + CPU safety.
 * Node port of voucher-expiry-contract-check.py (Windows without Python).
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const REPO = path.resolve(ROOT, "..");

const read = (p) => fs.readFileSync(p, "utf8");
const PSM = read(path.join(ROOT, "src", "PortalSessionManager.cpp"));
const VM = read(path.join(ROOT, "src", "VoucherManager.cpp"));
const DRIVER = read(
  path.join(ROOT, "src", "router", "drivers", "MikroTikDriver.cpp"),
);
const SALES = read(path.join(ROOT, "src", "SalesTime.cpp"));
const VOUCHERS_UI = read(path.join(REPO, "src", "pages", "VouchersPage.tsx"));

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

check("A generate stores profileName + minutes", () => {
  mustContain("minutes", VM, String.raw`item\["minutes"\] = minutes`);
  mustContain("profileName", VM, String.raw`item\["profileName"\] = profileName`);
});

check("B reserve stamps serviceExpiresAt from redeemedAt + minutes", () => {
  mustContain(
    "salesAddSecondsToIso",
    VM,
    String.raw`salesAddSecondsToIso\(\s*redeemedAt,\s*static_cast<uint32_t>\(minutes\)\s*\*\s*60U\)`,
  );
  mustContain(
    "serviceExpiresAt assign",
    VM,
    String.raw`item\["serviceExpiresAt"\] = stampedExpiry`,
  );
});

check("C activate preserves absolute serviceExpiresAt", () => {
  mustContain(
    "never extend",
    PSM,
    "Absolute expiry is stamped at redeem — never extend from activatedAt",
  );
  mustContain(
    "prefer session expiry",
    PSM,
    String.raw`serviceExpiresAt = String\(session\["serviceExpiresAt"\]`,
  );
  mustContain(
    "markActivated keep existing",
    VM,
    "Never overwrite an absolute expiry stamped at redeem",
  );
});

check("D reconnect rejects zero wall remaining", () => {
  mustContain(
    "reconnect expire",
    PSM,
    String.raw`reconnectVoucher[\s\S]{0,800}?remaining == 0[\s\S]{0,200}?mustExpire = true`,
  );
});

check("E onSessionActivated expires past-due voucher", () => {
  mustContain(
    "mustExpireVoucher",
    PSM,
    String.raw`mustExpireVoucher[\s\S]{0,1200}?ExpireSession`,
  );
});

check("F absolute expire when not Active enqueues ExpireSession", () => {
  mustContain(
    "not Active expire",
    PSM,
    "Absolute voucher expiry: enqueue ONE ExpireSession even when NOT Active",
  );
});

check("G boot recovery expires past-due vouchers", () => {
  mustContain(
    "boot voucher expiry",
    PSM,
    "Voucher absolute expiry survives reboot",
  );
});

check("H timerRunning requires connected", () => {
  mustContain(
    "timerRunning connected",
    PSM,
    String.raw`timerRunning"\] = state == PortalState::Active && !paused && secondsLeft > 0 &&\s*\(out\["connected"\]`,
  );
});

check("I hotspotProfile + admin profile select + ROS profile=", () => {
  mustContain(
    "hotspotProfile",
    PSM,
    String.raw`session\["hotspotProfile"\] = reserved\.profileName`,
  );
  mustContain("profiles API", VOUCHERS_UI, String.raw`routerApi\.profiles\(\)`);
  mustContain("user.profile", DRIVER, String.raw`=profile=" \+ profile`);
});

check("J coin Model B unchanged (no grace)", () => {
  mustContain(
    "model B",
    DRIVER,
    String.raw`new_limit = existing_uptime \+ requested_seconds`,
  );
  mustContain("no grace", DRIVER, "Do NOT add grace");
  mustNotContain(
    "graceSeconds",
    DRIVER,
    String.raw`graceSeconds\s*\+|kActivationGrace|GRACE_SECONDS`,
  );
});

check("K no tick→createHotspotUser; 60s coalesce verify; sales helpers", () => {
  mustContain("60s verify", PSM, "kVerifyIntervalMs = 60000");
  mustContain("no ROS from enrich", PSM, "ESP32-local only — 0 RouterOS commands");
  mustNotContain(
    "tick direct createHotspotUser",
    PSM,
    String.raw`tickSessions[\s\S]{0,200}?createHotspotUser`,
  );
  mustContain(
    "sales helpers",
    SALES,
    String.raw`salesAddSecondsToIso|salesSecondsUntilIso`,
  );
});

check("redeem entitlement from serviceExpiresAt when present", () => {
  mustContain(
    "entitlement from expiry",
    PSM,
    String.raw`Absolute voucher authority: remaining until redeemedAt\+validity`,
  );
});

const failed = checks.filter((ok) => !ok).length;
console.log(`\n${checks.length - failed}/${checks.length} passed`);
process.exit(failed ? 1 : 0);
