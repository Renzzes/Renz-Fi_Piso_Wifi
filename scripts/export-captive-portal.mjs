/**
 * After a successful build:mikrotik-portal, export the MikroTik upload overlay to
 * C:\Captive_Portal_BAT\ (and optional history under C:\Captive_Portal_BAT_HISTORY\).
 *
 * Invoked by scripts/export-captive-portal.bat — do not use stale Final_Build without rebuild.
 */
import { createHash } from "node:crypto";
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
import {
  APPLIANCE_BASE_URL_DEFAULT,
  MIKROTIK_UPLOAD_FILES,
  PLACEHOLDER,
  REQUIRED_GENERATED_FILES,
} from "./captive-portal-upload-files.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const deployDir = join(root, "deployment", "mikrotik-hotspot");
const exportDir = process.env.RENZFI_CAPTIVE_EXPORT_DIR || "C:\\Captive_Portal_BAT";
const historyRoot =
  process.env.RENZFI_CAPTIVE_EXPORT_HISTORY || "C:\\Captive_Portal_BAT_HISTORY";
const applianceUrl =
  (process.env.RENZFI_APPLIANCE_BASE_URL || APPLIANCE_BASE_URL_DEFAULT)
    .trim()
    .replace(/\/+$/, "");

function fail(msg) {
  console.error(`[export-captive-portal] ERROR: ${msg}`);
  process.exit(1);
}

function sha256File(path) {
  return createHash("sha256").update(readFileSync(path)).digest("hex");
}

function stamp() {
  const d = new Date();
  const p = (n) => String(n).padStart(2, "0");
  return (
    `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}_` +
    `${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`
  );
}

console.log("[export-captive-portal] Validating generated MikroTik bundle…");

for (const name of REQUIRED_GENERATED_FILES) {
  const p = join(deployDir, name);
  if (!existsSync(p)) fail(`Missing required generated file: ${p}`);
}

const appJs = join(deployDir, "renzfi-app.js");
const appText = readFileSync(appJs, "utf8");
if (/var\s+RENZFI_APPLIANCE_BASE_URL\s*=\s*["']__RENZFI_APPLIANCE_BASE_URL__["']/.test(appText)) {
  fail(
    `Generated renzfi-app.js production declaration still uses ${PLACEHOLDER} — refusing export`,
  );
}
if (!appText.includes(`var RENZFI_APPLIANCE_BASE_URL = ${JSON.stringify(applianceUrl)};`)) {
  fail(`Generated renzfi-app.js missing production declaration for ${applianceUrl}`);
}

const present = [];
for (const name of MIKROTIK_UPLOAD_FILES) {
  const p = join(deployDir, name);
  if (existsSync(p)) present.push(name);
  else if (!name.endsWith(".mp3")) fail(`Required upload file missing: ${p}`);
  else console.warn(`[export-captive-portal] WARN: optional missing: ${name}`);
}

// Preserve previous export as history before replacing active folder.
if (existsSync(exportDir)) {
  const existing = readdirSync(exportDir).filter((n) => n !== "_history");
  if (existing.length > 0) {
    const histDir = join(historyRoot, stamp());
    mkdirSync(histDir, { recursive: true });
    for (const name of existing) {
      const src = join(exportDir, name);
      try {
        cpSync(src, join(histDir, name), { recursive: true });
      } catch (err) {
        console.warn(
          `[export-captive-portal] WARN: could not backup ${name}: ${err.message}`,
        );
      }
    }
    console.log(`[export-captive-portal] Previous export backed up to: ${histDir}`);
  }
}

mkdirSync(exportDir, { recursive: true });

// Clear previous overlay files in active export (not the whole drive).
for (const name of readdirSync(exportDir)) {
  if (name === "_history") continue;
  rmSync(join(exportDir, name), { recursive: true, force: true });
}

const exported = [];
for (const name of present) {
  const src = join(deployDir, name);
  const dest = join(exportDir, name);
  cpSync(src, dest);
  const size = statSync(dest).size;
  const hash = sha256File(dest);
  exported.push({ name, size, sha256: hash });
  console.log(`  + ${name}  (${size} bytes)  sha256=${hash}`);
}

const lines = [
  "Renz-Fi Customer Captive Portal",
  `Build date/time: ${new Date().toISOString()}`,
  "",
  "Source directory:",
  "    portal/",
  "",
  "Build command:",
  `    RENZFI_APPLIANCE_BASE_URL=${applianceUrl} npm run build:mikrotik-portal`,
  "",
  "Generated deployment source:",
  "    deployment/mikrotik-hotspot/",
  "",
  "Export directory:",
  `    ${exportDir}`,
  "",
  "Appliance API:",
  `    ${applianceUrl}`,
  "",
  "IMPORTANT:",
  "  - Edit ONLY portal/ in the repo.",
  "  - Never hand-edit this export folder as source.",
  "  - Upload these files as an OVERLAY into the router's existing hotspot/",
  "    directory. You MUST overwrite status.html (it is the Renz-Fi /status page).",
  "  - Do not delete native Hotspot servlets: alogin.html, redirect.html,",
  "    logout.html, error.html.",
  "",
  "Files exported:",
];

for (const f of exported) {
  lines.push(`  - ${f.name}`);
  lines.push(`      size:   ${f.size} bytes`);
  lines.push(`      sha256: ${f.sha256}`);
}
lines.push("");

const infoPath = join(exportDir, "CAPTIVE_PORTAL_BUILD_INFO.txt");
writeFileSync(infoPath, lines.join("\r\n"), "utf8");
console.log(`[export-captive-portal] Wrote ${infoPath}`);
console.log(`[export-captive-portal] OK — upload package ready at ${exportDir}`);
console.log("[export-captive-portal] Open that folder and upload its files to MikroTik hotspot/.");
