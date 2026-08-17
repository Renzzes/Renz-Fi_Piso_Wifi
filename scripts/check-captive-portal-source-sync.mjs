/**
 * Verify generated MikroTik portal artifacts match portal/ source (after URL sub).
 * Read-only — does not overwrite files.
 *
 * Usage: node scripts/check-captive-portal-source-sync.mjs
 * Exit 0 = OK, 1 = FAIL
 */
import { createHash } from "node:crypto";
import { existsSync, readFileSync, statSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  APPLIANCE_BASE_URL_DEFAULT,
  MIKROTIK_UPLOAD_FILES,
  PLACEHOLDER,
  REQUIRED_GENERATED_FILES,
} from "./captive-portal-upload-files.mjs";
import { PORTAL_SOURCE_DIR } from "./esp32-staging-manifest.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const portalDir = join(root, PORTAL_SOURCE_DIR);
const deployDir = join(root, "deployment", "mikrotik-hotspot");
const finalDir = join(root, "Final_Build_Portal");
const applianceUrl =
  (process.env.RENZFI_APPLIANCE_BASE_URL || APPLIANCE_BASE_URL_DEFAULT)
    .trim()
    .replace(/\/+$/, "");

const errors = [];
const notes = [];

function sha256(buf) {
  return createHash("sha256").update(buf).digest("hex");
}

function fail(msg) {
  errors.push(msg);
}

function expectFile(dir, name, label) {
  const p = join(dir, name);
  if (!existsSync(p)) {
    fail(`Missing ${label}: ${p}`);
    return null;
  }
  return p;
}

console.log("[portal-sync] Checking captive portal source → generated consistency");
console.log(`[portal-sync] Source: ${portalDir}`);
console.log(`[portal-sync] Generated: ${deployDir}`);
console.log(`[portal-sync] Release overlay: ${finalDir}`);
console.log(`[portal-sync] Expected appliance URL: ${applianceUrl}`);

if (!existsSync(portalDir)) fail(`Missing canonical source directory: ${portalDir}`);
if (!existsSync(join(root, "package.json"))) fail("Missing package.json at repo root");

for (const name of REQUIRED_GENERATED_FILES) {
  expectFile(deployDir, name, "deployment");
  expectFile(finalDir, name, "Final_Build_Portal");
}

const portalApp = join(portalDir, "renzfi-app.js");
const deployApp = join(deployDir, "renzfi-app.js");
const finalApp = join(finalDir, "renzfi-app.js");

if (existsSync(portalApp) && existsSync(deployApp)) {
  const portalRaw = readFileSync(portalApp, "utf8");
  const deployRaw = readFileSync(deployApp, "utf8");
  const finalRaw = existsSync(finalApp) ? readFileSync(finalApp, "utf8") : null;

  if (!portalRaw.includes(`var RENZFI_APPLIANCE_BASE_URL = "${PLACEHOLDER}";`)) {
    fail(
      `portal/renzfi-app.js must keep placeholder declaration with ${PLACEHOLDER}`,
    );
  }

  if (/var\s+RENZFI_APPLIANCE_BASE_URL\s*=\s*["']__RENZFI_APPLIANCE_BASE_URL__["']/.test(deployRaw)) {
    fail(
      "FAIL — generated portal is stale or incomplete\n" +
        `  Source: ${portalApp}\n` +
        `  Generated: ${deployApp}\n` +
        `  Reason: production declaration still uses ${PLACEHOLDER}`,
    );
  }

  if (!deployRaw.includes(`var RENZFI_APPLIANCE_BASE_URL = ${JSON.stringify(applianceUrl)};`)) {
    fail(
      `deployment/mikrotik-hotspot/renzfi-app.js missing production declaration for ${applianceUrl}`,
    );
  }

  const expected =
    portalRaw.replace(
      `var RENZFI_APPLIANCE_BASE_URL = "${PLACEHOLDER}";`,
      `var RENZFI_APPLIANCE_BASE_URL = ${JSON.stringify(applianceUrl)};`,
    );

  if (expected === portalRaw) {
    fail("Could not apply placeholder substitution for comparison");
  } else if (expected !== deployRaw) {
    fail(
      "FAIL — generated portal is stale\n" +
        `  Source: ${portalApp}\n` +
        `  Generated: ${deployApp}\n` +
        `  Reason: deployment/renzfi-app.js does not match portal/ after URL substitution\n` +
        `  sourceSha=${sha256(portalRaw)} expectedSha=${sha256(expected)} generatedSha=${sha256(deployRaw)}`,
    );
  } else {
    notes.push("renzfi-app.js: deployment matches portal + URL substitution");
  }

  if (finalRaw !== null) {
    if (finalRaw !== deployRaw) {
      fail(
        "FAIL — Final_Build_Portal/renzfi-app.js differs from deployment/mikrotik-hotspot/renzfi-app.js",
      );
    } else {
      notes.push("renzfi-app.js: Final_Build_Portal identical to deployment");
    }
  }
}

const verbatim = [
  "login.html",
  "status.html",
  "renzfi-style.css",
  "md5.js",
  "Default-Banner.png",
  "bg_music.mp3",
  "coin.mp3",
  "success.mp3",
];

for (const name of verbatim) {
  const src = join(portalDir, name);
  const gen = join(deployDir, name);
  const fin = join(finalDir, name);
  if (!existsSync(src)) {
    if (MIKROTIK_UPLOAD_FILES.includes(name) && name.endsWith(".mp3")) {
      notes.push(`optional missing in source (ok if absent): ${name}`);
      continue;
    }
    if (["login.html", "status.html", "renzfi-style.css", "md5.js"].includes(name)) {
      fail(`Missing source file: ${src}`);
    }
    continue;
  }
  if (!existsSync(gen)) {
    fail(`Missing generated file: ${gen}`);
    continue;
  }
  const sBuf = readFileSync(src);
  const gBuf = readFileSync(gen);
  if (sha256(sBuf) !== sha256(gBuf)) {
    fail(
      `FAIL — generated portal is stale\n` +
        `  Source: ${src}\n` +
        `  Generated: ${gen}\n` +
        `  Reason: SHA256 mismatch (edit portal/, then rebuild)`,
    );
  } else {
    notes.push(`${name}: deployment matches portal (${statSync(gen).size} bytes)`);
  }
  if (existsSync(fin) && sha256(readFileSync(fin)) !== sha256(gBuf)) {
    fail(`Final_Build_Portal/${name} differs from deployment copy`);
  }
}

for (const name of MIKROTIK_UPLOAD_FILES) {
  const gen = join(deployDir, name);
  if (!existsSync(gen) && !name.endsWith(".mp3")) {
    fail(`Required upload file missing from deployment: ${name}`);
  }
}

if (errors.length) {
  console.error("\n[portal-sync] FAIL — generated portal is stale or inconsistent");
  for (const e of errors) console.error(`  - ${e}`);
  console.error(
    "\nFix: edit only portal/, then:\n" +
      `  set RENZFI_APPLIANCE_BASE_URL=${applianceUrl}\n` +
      "  npm run build:mikrotik-portal\n" +
      "  OR run scripts\\export-captive-portal.bat",
  );
  process.exit(1);
}

console.log("\n[portal-sync] OK — generated portal matches source build");
for (const n of notes) console.log(`  - ${n}`);
process.exit(0);
