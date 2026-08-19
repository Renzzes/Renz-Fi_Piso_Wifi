#!/usr/bin/env node
/**
 * Architectural guardrails for External Access Point.
 * Locks the product model: register + reachability only. Never AP configuration.
 *
 * Run: node scripts/test-access-point-architecture-guardrails.mjs
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function read(rel) {
  return fs.readFileSync(path.join(root, rel), "utf8");
}

function exists(rel) {
  return fs.existsSync(path.join(root, rel));
}

let failed = 0;
function assert(name, condition) {
  if (!condition) {
    console.error(`FAIL ${name}`);
    failed += 1;
    return;
  }
  console.log(`ok ${name}`);
}

const architecture = read("docs/EXTERNAL_ACCESS_POINT_ARCHITECTURE.md");
const requiredStatements = [
  "Renz-Fi does not configure external APs.",
  "AP credentials are metadata only and are not used for AP login/configuration.",
  "Vendor is informational metadata and is not a driver selector.",
  "GenericApDriver is a reachability probe, not a configuration driver.",
  "No VLAN is implemented.",
  "No AP-side bandwidth enforcement is implemented.",
  "MikroTik remains the network authority.",
  "Owner configures AP through the AP's own web interface.",
  "Open launches the AP's own management interface; ESP32 does not proxy it.",
  "Same SSID is allowed but seamless roaming is not guaranteed.",
];

for (const statement of requiredStatements) {
  assert(`architecture: ${statement}`, architecture.includes(statement));
}

const routeContract = read("ESP32_S3_Firmware/docs/HTTP_ROUTE_CONTRACT.md");
assert(
  "HTTP_ROUTE_CONTRACT Check does not configure the AP",
  /\/api\/access-points\/\{id\}\/check/.test(routeContract) &&
    routeContract.includes("does not configure the AP"),
);

const roleMatrix = read("ESP32_S3_Firmware/docs/ROLE_PERMISSION_MATRIX.md");
assert(
  "ROLE_PERMISSION_MATRIX Check does not configure the AP",
  roleMatrix.includes("/api/access-points/{id}/check") &&
    roleMatrix.includes("does not configure the AP"),
);

const driverCpp = read("ESP32_S3_Firmware/src/ap/GenericApDriver.cpp");
const driverH = read("ESP32_S3_Firmware/src/ap/GenericApDriver.h");
assert("GenericApDriver has no HTTP request", !/\bHTTP_(GET|POST|PUT|PATCH|DELETE)\b/.test(driverCpp));
assert("GenericApDriver has no Authorization header", !/Authorization/i.test(driverCpp));
assert("GenericApDriver has no credential fields", !/password|username|unprotectSecret/i.test(driverCpp));
assert(
  "GenericApDriver header is reachability-only",
  driverH.includes("Reachability only") && /no configuration/i.test(driverH),
);

const iface = read("ESP32_S3_Firmware/src/ap/IExternalApDriver.h");
assert(
  "IExternalApDriver probe target is IP only",
  iface.includes("managementIp") && !/password|username|ssid|vlan/i.test(iface),
);
assert("IExternalApDriver is documented as reachability probe", /reachability/i.test(iface));

const managerCpp = read("ESP32_S3_Firmware/src/ExternalAccessPointManager.cpp");
const enqueueStart = managerCpp.indexOf("ExternalAccessPointManager::enqueueCheck");
const runStart = managerCpp.indexOf("ExternalAccessPointManager::runQueuedJob");
assert("enqueueCheck exists", enqueueStart >= 0);
assert("runQueuedJob exists", runStart >= 0);
const enqueueFn = managerCpp.slice(enqueueStart, runStart > enqueueStart ? runStart : undefined);
const runFn = managerCpp.slice(runStart);
assert("check enqueue does not writeJson", !enqueueFn.includes("writeJson"));
assert("check enqueue does not unprotectSecret", !enqueueFn.includes("unprotectSecret"));
assert("check worker does not writeJson", !runFn.includes("writeJson"));
assert("check worker does not unprotectSecret", !runFn.includes("unprotectSecret"));
assert("check worker does not call RouterOS", !/MikroTikDriver|RouterOsClient|RouterProvisioningWorker/.test(runFn));
assert("check worker copies IP not credentials", runFn.includes("managementIp") && !runFn.includes("passwordProtected"));
assert("probe target is management IP only", runFn.includes("target.managementIp = ip"));

assert("no TpLinkApDriver file", !exists("ESP32_S3_Firmware/src/ap/TpLinkApDriver.cpp"));
assert("no RuijieApDriver file", !exists("ESP32_S3_Firmware/src/ap/RuijieApDriver.cpp"));
assert("no TendaApDriver file", !exists("ESP32_S3_Firmware/src/ap/TendaApDriver.cpp"));

const page = read("src/pages/AccessPointsPage.tsx");
assert("Open uses window.open http://ip", page.includes("window.open(`http://${trimmed}`"));
assert("Open does not proxy through ESP32", !/\/api\/access-points\/.*proxy|reverse.?proxy/i.test(page));
assert("Admin has no VLAN control", !/\bid=["']ap-vlan["']|name=["']vlan["']|VLAN ID/i.test(page));
assert("Admin has no AP bandwidth control", !/bandwidth|rateLimit|rate-limit|qos/i.test(page));
assert("Admin does not persist AP password in localStorage", !/localStorage[\s\S]{0,80}password/i.test(page));
assert("vendor is labeled informational", page.includes("Brand (label only)"));
assert("page states Renz-Fi does not configure the access point", page.includes("Renz-Fi does not configure the access point"));

const types = read("ESP32_S3_Firmware/src/ExternalAccessPointTypes.h");
assert(
  "classifyReachability never returns AuthFailed",
  types.includes("classifyReachability") &&
    !/classifyReachability[\s\S]{0,400}AuthFailed/.test(types),
);

if (failed) {
  console.error(`access-point architecture guardrails failed (${failed})`);
  process.exit(1);
}
console.log("access-point architecture guardrails passed");
