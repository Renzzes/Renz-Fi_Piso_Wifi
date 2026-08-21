#!/usr/bin/env node
/**
 * Host guard: operational SPIFFS fallback allowlist vs bulk SD-primary data.
 * Mirrors StorageManager::isFallbackEligible + N16R8 / Waveshare contract.
 *
 * Run: node scripts/test-storage-fallback-classification.mjs
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const storageCpp = fs.readFileSync(
  path.join(ROOT, "ESP32_S3_Firmware", "src", "StorageManager.cpp"),
  "utf8",
);
const workerCpp = fs.readFileSync(
  path.join(ROOT, "ESP32_S3_Firmware", "src", "RouterProvisioningWorker.cpp"),
  "utf8",
);
const contractDoc = fs.readFileSync(
  path.join(ROOT, "docs", "WAVESHARE_STORAGE_RESILIENCE_CONTRACT.md"),
  "utf8",
);

function assert(cond, msg) {
  if (!cond) throw new Error(msg);
}

const eligible = storageCpp.slice(
  storageCpp.indexOf("bool StorageManager::isFallbackEligible"),
  storageCpp.indexOf("bool StorageManager::isContinuousCheckpointEligible"),
);

const mustAllow = [
  "SETTINGS_FILE",
  "PROMOS_FILE",
  "ROUTER_FILE",
  "VOUCHERS_FILE",
  "PORTAL_SESSIONS_FILE",
  "SALES_FILE",
  "PORTAL_CONFIG_FILE",
  "InstallationFile",
  "ProvisioningFile",
  "RouterConnectionFile",
  "RouterProvisioningFile",
  "SetupWizardFile",
];

for (const token of mustAllow) {
  assert(eligible.includes(token), `fallback eligible must include ${token}`);
}

// Bulk / non-operational paths must not be silently mirrored as unrestricted
// internal databases (path tokens that must stay out of isFallbackEligible).
assert(
  !eligible.includes("LOGS_FILE"),
  "logs must not be unrestricted SPIFFS fallback mirrors",
);
assert(
  !/history/i.test(eligible),
  "history ledgers must not be in isFallbackEligible",
);

assert(
  storageCpp.includes("SPIFFS fallback seeded"),
  "boot-without-SD must seed SPIFFS operational checkpoints",
);
assert(
  storageCpp.includes("FB_INSTALLATION"),
  "boot seed must include installation checkpoint",
);

assert(
  /SdLifecycle::Degraded:\s*\n\s*default:\s*\n\s*return false/m.test(workerCpp),
  "steady SD_DEGRADED must not permanently block Admin RouterOS enqueue",
);
assert(
  workerCpp.includes("SdLifecycle::Remounting"),
  "active Remounting must still gate Admin RouterOS",
);
assert(
  workerCpp.includes("SdLifecycle::Syncing"),
  "active Syncing must still gate Admin RouterOS",
);

assert(
  contractDoc.includes("operational resilience layer"),
  "Waveshare contract doc must state internal storage role",
);
assert(
  contractDoc.includes("replacement for SD"),
  "Waveshare contract doc must forbid SD replacement",
);

console.log("test-storage-fallback-classification: ok");
