/**
 * Build the production captive portal bundle for MikroTik Hotspot storage.
 *
 * Pipeline:
 *   portal/  →  validate  →  substitute RENZFI_APPLIANCE_BASE_URL  →
 *   deployment/mikrotik-hotspot/
 *
 * Do NOT edit deployment/mikrotik-hotspot/*.{html,js,css,ico,png,mp3} manually —
 * always run npm run build:mikrotik-portal. README.md, upload-hotspot-files.rsc,
 * and MIGRATION_192_TO_10_10_10.md in that directory are hand-written templates.
 */
import {
  cpSync,
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { ensurePortalSources } from "./ensure-portal-sources.mjs";
import { PORTAL_SOURCE_DIR } from "./esp32-staging-manifest.mjs";
import {
  PLACEHOLDER,
  validateGeneratedAppJs,
  validateRouterOsTokens,
  validateStatusHtml,
} from "./portal-resolver.mjs";
import { MIKROTIK_UPLOAD_FILES } from "./captive-portal-upload-files.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const target = join(root, "deployment", "mikrotik-hotspot");
const finalTarget = join(root, "Final_Build_Portal");

const FORBIDDEN_IPS = ["10.40.0.2", "192.168.88.2"];

const COPY_VERBATIM = [
  "login.html",
  "status.html",
  "renzfi-style.css",
  "md5.js",
  "favicon.ico",
  "Default-Banner.png",
];

/** Owner Admin launcher — placeholder substituted like renzfi-app.js */
const ADMIN_LAUNCHER = "admin.html";

const COPY_IF_PRESENT = ["bg_music.mp3", "coin.mp3", "success.mp3"];
/** Owner MikroTik overlay list — shared with export BAT / sync check. */
const FINAL_UPLOAD_FILES = MIKROTIK_UPLOAD_FILES;

const log = (msg) => console.log(`[build:mikrotik-portal] ${msg}`);
const fail = (msg) => {
  console.error(`[build:mikrotik-portal] ERROR: ${msg}`);
  process.exit(1);
};

function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

log("Ensuring portal sources...");
await ensurePortalSources();

const portalSource = join(root, PORTAL_SOURCE_DIR);
mkdirSync(target, { recursive: true });

const rawBaseUrl = process.env.RENZFI_APPLIANCE_BASE_URL;
if (!rawBaseUrl || !String(rawBaseUrl).trim()) {
  fail(
    "RENZFI_APPLIANCE_BASE_URL is required.\n" +
      "Example:\n" +
      "  RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal\n" +
      "Use the ESP32 DHCP reservation or current LAN address on the hAP lite guest network.",
  );
}

const baseUrl = String(rawBaseUrl).trim().replace(/\/+$/, "");
if (!/^https?:\/\/.+/i.test(baseUrl)) {
  fail(`RENZFI_APPLIANCE_BASE_URL must be an absolute http(s) URL, got: "${baseUrl}"`);
}

for (const forbidden of FORBIDDEN_IPS) {
  if (baseUrl.includes(forbidden)) {
    fail(`RENZFI_APPLIANCE_BASE_URL must not use stale address ${forbidden}`);
  }
}

log(`Appliance base URL: ${baseUrl}`);

const filesCopied = [];

for (const name of COPY_VERBATIM) {
  const src = join(portalSource, name);
  if (!existsSync(src)) fail(`Missing required portal source: ${name}`);
  const dest = join(target, name);
  cpSync(src, dest);
  filesCopied.push(dest);
}

const missingAudio = [];
for (const name of COPY_IF_PRESENT) {
  const src = join(portalSource, name);
  const dest = join(target, name);
  if (existsSync(src)) {
    cpSync(src, dest);
    filesCopied.push(dest);
  } else {
    missingAudio.push(name);
  }
}

if (missingAudio.length > 0) {
  log(
    "WARN: optional MikroTik default audio missing from portal/ — add before production upload:",
  );
  for (const name of missingAudio) log(`  - ${name}`);
}

const appJsSrc = join(portalSource, "renzfi-app.js");
if (!existsSync(appJsSrc)) fail("Missing required portal source: renzfi-app.js");

const appJsRaw = readFileSync(appJsSrc, "utf8");
const DECLARATION = `var RENZFI_APPLIANCE_BASE_URL = "${PLACEHOLDER}";`;

if (!appJsRaw.includes(DECLARATION)) {
  fail(
    `portal/renzfi-app.js does not contain the expected declaration:\n  ${DECLARATION}\n` +
      "Has the resolver in portal/renzfi-app.js been changed? Update this build script to match.",
  );
}

const replacement = `var RENZFI_APPLIANCE_BASE_URL = ${JSON.stringify(baseUrl)};`;
const appJsOut = appJsRaw.replace(DECLARATION, replacement);

if (appJsOut === appJsRaw || !appJsOut.includes(replacement)) {
  fail("Failed to substitute RENZFI_APPLIANCE_BASE_URL declaration — this is a bug.");
}
if (!appJsOut.includes(PLACEHOLDER)) {
  fail(
    `Placeholder ${PLACEHOLDER} unexpectedly missing from isPlaceholder() detection logic after ` +
      "substitution — the resolver's own fallback check may be broken. This is a bug.",
  );
}

if (/var\s+RENZFI_APPLIANCE_BASE_URL\s*=\s*["']__RENZFI_APPLIANCE_BASE_URL__["']/.test(appJsOut)) {
  fail("Unresolved __RENZFI_APPLIANCE_BASE_URL__ remains in production declaration — FAIL BUILD");
}

const jsErrors = validateGeneratedAppJs(appJsOut, FORBIDDEN_IPS);
if (jsErrors.length > 0) fail(`Generated renzfi-app.js validation failed:\n  - ${jsErrors.join("\n  - ")}`);

const loginHtml = readFileSync(join(target, "login.html"), "utf8");
const missingTokens = validateRouterOsTokens(loginHtml);
if (missingTokens.length > 0) {
  fail(`login.html is missing RouterOS template tokens: ${missingTokens.join(", ")}`);
}

const statusHtml = readFileSync(join(target, "status.html"), "utf8");
const statusErrors = validateStatusHtml(statusHtml);
if (statusErrors.length > 0) {
  fail(`status.html validation failed:\n  - ${statusErrors.join("\n  - ")}`);
}

const appJsDest = join(target, "renzfi-app.js");
writeFileSync(appJsDest, appJsOut, "utf8");
filesCopied.push(appJsDest);

const adminSrc = join(portalSource, ADMIN_LAUNCHER);
if (!existsSync(adminSrc)) fail(`Missing required portal source: ${ADMIN_LAUNCHER}`);
const adminRaw = readFileSync(adminSrc, "utf8");
if (!adminRaw.includes(PLACEHOLDER)) {
  fail(
    `${ADMIN_LAUNCHER} must contain ${PLACEHOLDER} so the build can inject RENZFI_APPLIANCE_BASE_URL`,
  );
}
const adminOut = adminRaw.split(PLACEHOLDER).join(baseUrl);
if (adminOut.includes(PLACEHOLDER)) {
  fail(`Unresolved ${PLACEHOLDER} remains in ${ADMIN_LAUNCHER}`);
}
for (const forbidden of FORBIDDEN_IPS) {
  if (adminOut.includes(forbidden)) {
    fail(`${ADMIN_LAUNCHER} must not contain stale address ${forbidden}`);
  }
}
if (!adminOut.includes(`${baseUrl}/admin`)) {
  fail(`${ADMIN_LAUNCHER} must redirect to ${baseUrl}/admin after substitution`);
}
const adminDest = join(target, ADMIN_LAUNCHER);
writeFileSync(adminDest, adminOut, "utf8");
filesCopied.push(adminDest);
log(`Admin launcher: ${ADMIN_LAUNCHER} → ${baseUrl}/admin`);

mkdirSync(finalTarget, { recursive: true });
for (const name of readdirSync(finalTarget)) {
  if (name !== "README.md") {
    rmSync(join(finalTarget, name), { recursive: true, force: true });
  }
}
for (const name of FINAL_UPLOAD_FILES) {
  const src = join(target, name);
  if (!existsSync(src)) {
    fail(`Final_Build_Portal requires missing generated file: ${name}`);
  }
  cpSync(src, join(finalTarget, name));
}
log("Owner upload overlay synchronized: Final_Build_Portal/");

let totalBytes = 0;
log("Files copied:");
for (const file of filesCopied) {
  const { size } = statSync(file);
  totalBytes += size;
  console.log(`  - ${file.slice(root.length + 1)} (${formatBytes(size)})`);
}

log(`Total bundle size: ${formatBytes(totalBytes)}`);
log("Ready to upload — see deployment/mikrotik-hotspot/README.md");
