#!/usr/bin/env node
/**
 * Static contract: owner_created requires Setup Unlock Password instead of
 * owner recreation. Source-only — does not flash hardware.
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");

const read = (p) => fs.readFileSync(p, "utf8");
const SETUP = read(path.join(ROOT, "src", "web", "SetupServer.cpp"));
const PROV = read(path.join(ROOT, "src", "SetupProvisioningManager.cpp"));
const PROV_H = read(path.join(ROOT, "src", "SetupProvisioningManager.h"));
const API = read(path.join(ROOT, "src", "ApiServer.cpp"));
const AUTH = read(path.join(ROOT, "src", "AuthCredentials.cpp"));

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

check("1 unlock endpoint already exists", () => {
  mustContain("POST /api/setup/unlock", SETUP, String.raw`"/api/setup/unlock"`);
  mustContain("unlockSetup", SETUP, "unlockSetup");
  mustContain("invalid code", SETUP, "SETUP_UNLOCK_INVALID");
});

check("2 owner_created requires unlock password", () => {
  mustContain(
    "requiresSetupUnlock includes ownerCreated",
    PROV,
    String.raw`bool SetupProvisioningManager::requiresSetupUnlock[\s\S]{0,500}?_ownerCreated`,
  );
  mustContain("setupUnlockPasswordHash persisted", PROV, "setupUnlockPasswordHash");
  mustContain("hash verify", PROV_H, "verifySetupUnlockPassword");
});

check("3 lock page is shown without requiring Ready", () => {
  mustContain(
    "serveSetupPage lock ignores isReady",
    SETUP,
    String.raw`const bool locked =[\s\S]{0,180}?requiresSetupUnlock\(\)`,
  );
  mustNotContain(
    "lock no longer requires isReady",
    SETUP,
    String.raw`const bool locked =[\s\S]{0,120}?isReady\(\)`,
  );
  mustContain("lock copy owner exists", SETUP, "An owner account has already been created");
  mustContain("Unlock Setup button", SETUP, "Unlock Setup");
});

check("4 owner creation still rejected when owner exists", () => {
  mustContain("OWNER_ALREADY_EXISTS", PROV, "OWNER_ALREADY_EXISTS");
  mustContain(
    "createOwner rejects existing owner",
    PROV,
    String.raw`SetupProvisioningManager::createOwner[\s\S]{0,400}?OWNER_ALREADY_EXISTS`,
  );
});

check("5 unlock does not recreate owner or reset factory", () => {
  mustNotContain(
    "unlockSetup no setState Factory",
    PROV,
    String.raw`bool SetupProvisioningManager::unlockSetup[\s\S]{0,400}?Factory`,
  );
  mustNotContain(
    "unlockSetup no createOwner",
    PROV,
    String.raw`bool SetupProvisioningManager::unlockSetup[\s\S]{0,400}?createOwner`,
  );
  mustContain(
    "status still reports installationState",
    PROV,
    String.raw`void SetupProvisioningManager::fillSetupStatus[\s\S]{0,500}?installationState`,
  );
  mustContain("status reports wizardStep", PROV, String.raw`data\["wizardStep"\]`);
  mustContain("status reports setupLocked", PROV, String.raw`data\["setupLocked"\]`);
});

check("7 settings GET is owner-only and never returns the hash", () => {
  const start = API.indexOf('_server->on("/api/settings/setup-unlock", HTTP_GET');
  if (start < 0) throw new Error("GET /api/settings/setup-unlock missing");
  const next = API.indexOf("_server->on(", start + 10);
  const handler = API.slice(start, next > start ? next : start + 2500);
  mustContain("owner auth", handler, "requireOwnerAuth");
  mustContain("configured flag", handler, String.raw`\["configured"\]`);
  mustContain("recover from RAM blob", handler, "recoverSetupUnlockPassword");
  mustNotContain("no hash in GET", handler, "setupUnlockPasswordHash");
  mustNotContain("no default password leak", handler, "renzfi-setup");
  mustContain("one-way SHA-256 still used", AUTH, "mbedtls_sha256_starts");
  mustContain("persist hash", PROV, String.raw`doc\["setupUnlockPasswordHash"\] = _setupUnlockPasswordHash`);
  mustContain(
    "persist protected blob",
    PROV,
    String.raw`doc\["setupUnlockPasswordProtected"\] = _setupUnlockPasswordProtected`,
  );
  mustNotContain(
    "do not persist plaintext unlock password",
    PROV,
    String.raw`doc\["setupUnlockPassword"\]`,
  );
  mustContain("unlock still verifies hash", PROV, String.raw`hashPassword\(password\) == _setupUnlockPasswordHash`);
  mustNotContain(
    "setup status does not include unlock password",
    PROV,
    String.raw`void SetupProvisioningManager::fillSetupStatus[\s\S]{0,1800}?data\["password"\]`,
  );
});

check("6 GET /api/setup/status and unlock stay ungated", () => {
  mustNotContain(
    "status route not wizard-gated",
    SETUP,
    String.raw`"/api/setup/status"[\s\S]{0,400}?ensureSetupWizardEnabled`,
  );
  mustNotContain(
    "unlock route not wizard-gated",
    SETUP,
    String.raw`"/api/setup/unlock"[\s\S]{0,500}?ensureSetupWizardEnabled`,
  );
});

const failed = checks.filter((c) => !c).length;
console.log(
  failed === 0
    ? `\nAll ${checks.length} setup-unlock contracts PASS`
    : `\n${failed}/${checks.length} setup-unlock contracts FAILED`,
);
process.exit(failed === 0 ? 0 : 1);
