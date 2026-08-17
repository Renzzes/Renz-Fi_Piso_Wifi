/**
 * Stage React admin + captive portal *recovery* assets into ESP32_S3_Firmware/data/
 *
 * Pipeline:
 *   dist/  +  portal/ (required HTML/JS/CSS only)  →  ESP32_S3_Firmware/data/
 *
 * Production customer portal is MikroTik Hotspot (deployment/mikrotik-hotspot/).
 * ESP32 SPIFFS keeps a small portal/ set for Setup Finish provisioning + recovery.
 * Large portal audio (bg_music.mp3 etc.) is NOT staged — MikroTik-only.
 *
 * Do NOT edit data/ manually — always run npm run build:esp32
 */
import {
  cpSync,
  existsSync,
  mkdirSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { ensurePortalSources } from "./ensure-portal-sources.mjs";
import { generateBuildMetadata } from "./generate-build-metadata.mjs";
import {
  ADMIN_ROOT_FILES,
  ESP32_DATA_DIR,
  PORTAL_RECOMMENDED,
  PORTAL_REQUIRED,
  PORTAL_SOURCE_DIR,
  SPIFFS_MAX_OBJECT_NAME,
} from "./esp32-staging-manifest.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const dist = join(root, "dist");
const target = join(root, ESP32_DATA_DIR);
const distAssets = join(dist, "assets");
const targetAssets = join(target, "assets");
const portalSource = join(root, PORTAL_SOURCE_DIR);
const targetPortal = join(target, "portal");

const log = (msg) => console.log(`[stage:esp32] ${msg}`);
const fail = (msg) => {
  console.error(`[stage:esp32] ERROR: ${msg}`);
  process.exit(1);
};

function collectSpiffsPaths(dir, prefix = "") {
  const paths = [];
  if (!existsSync(dir)) return paths;
  for (const name of readdirSync(dir)) {
    const full = join(dir, name);
    const objectPath = `${prefix}/${name}`.replace(/\\/g, "/");
    if (statSync(full).isDirectory()) {
      paths.push(...collectSpiffsPaths(full, objectPath));
    } else {
      paths.push(objectPath);
    }
  }
  return paths;
}

function assertSpiffsSafePaths(baseDir) {
  const violations = collectSpiffsPaths(baseDir).filter(
    (objectPath) => objectPath.length > SPIFFS_MAX_OBJECT_NAME,
  );
  if (violations.length === 0) return;
  fail(
    `${violations.length} path(s) exceed SPIFFS ${SPIFFS_MAX_OBJECT_NAME}-byte limit:\n` +
      violations.map((p) => `  ${p.length} chars: ${p}`).join("\n"),
  );
}

function printPortalValidation() {
  log("Checking portal assets...");
  let ok = true;
  for (const { source, label } of PORTAL_REQUIRED) {
    const present = existsSync(join(targetPortal, source));
    console.log(`  ${present ? "✓" : "✗"} ${label}`);
    if (!present) ok = false;
  }
  for (const { source, label } of PORTAL_RECOMMENDED) {
    const present = existsSync(join(targetPortal, source));
    if (present) console.log(`  ✓ ${label} (optional)`);
  }
  if (!ok) {
    fail("Portal staging incomplete — fix portal/ sources and re-run npm run build:esp32");
  }
  log("Portal assets verified");
}

function printAdminValidation() {
  log("Checking admin dashboard assets...");
  if (!existsSync(join(target, "index.html"))) fail("Missing data/index.html");
  if (!existsSync(targetAssets)) fail("Missing data/assets/");
  const assetFiles = readdirSync(targetAssets).filter((n) =>
    statSync(join(targetAssets, n)).isFile(),
  );
  const hasJs = assetFiles.some((n) => n.endsWith(".js"));
  const hasCss = assetFiles.some((n) => n.endsWith(".css"));
  if (!hasJs || !hasCss) {
    fail(`data/assets/ must contain .js and .css bundles (found: ${assetFiles.join(", ")})`);
  }
  console.log(`  ✓ index.html`);
  console.log(`  ✓ ${assetFiles.filter((n) => n.endsWith(".js")).length} JS bundle(s)`);
  console.log(`  ✓ ${assetFiles.filter((n) => n.endsWith(".css")).length} CSS bundle(s)`);
  log("Admin dashboard verified");
}

// ── 1. Portal sources ───────────────────────────────────────────────────────

log("Ensuring portal sources...");
await ensurePortalSources();

// ── 2. React build (dist/) ──────────────────────────────────────────────────

if (!existsSync(dist)) {
  fail("dist/ not found. Run npm run build first.");
}
if (!existsSync(join(dist, "sw.js"))) fail("dist/sw.js not found");
if (!existsSync(join(dist, "index.html"))) fail("dist/index.html not found");
if (!existsSync(distAssets)) fail("dist/assets/ not found");
assertSpiffsSafePaths(dist);

// ── 3. Clean generated data/ ───────────────────────────────────────────────

log(`Cleaning ${ESP32_DATA_DIR}/ ...`);
if (existsSync(target)) {
  rmSync(target, { recursive: true, force: true });
}
mkdirSync(target, { recursive: true });

// ── 4. Stage admin ───────────────────────────────────────────────────────────

log("Staging admin dashboard...");
for (const name of ADMIN_ROOT_FILES) {
  const src = join(dist, name);
  if (existsSync(src)) cpSync(src, join(target, name));
}

const distIcons = join(dist, "icons");
const targetIcons = join(target, "icons");
if (existsSync(distIcons)) {
  mkdirSync(targetIcons, { recursive: true });
  cpSync(distIcons, targetIcons, { recursive: true });
} else {
  console.warn("[stage:esp32] WARN: dist/icons/ missing — run npm run pwa:icons");
}

mkdirSync(targetAssets, { recursive: true });
cpSync(distAssets, targetAssets, { recursive: true });

// ── 5. Stage portal ─────────────────────────────────────────────────────────

log("Staging captive portal...");
mkdirSync(targetPortal, { recursive: true });
for (const { source } of [...PORTAL_REQUIRED, ...PORTAL_RECOMMENDED]) {
  const src = join(portalSource, source);
  if (existsSync(src)) {
    cpSync(src, join(targetPortal, source));
  }
}

const buildInfo = generateBuildMetadata({ root, distDir: dist, portalDir: portalSource });

writeFileSync(
  join(target, "build-info.json"),
  JSON.stringify(buildInfo, null, 2),
);
log(`Build metadata: firmware=${buildInfo.firmwareVersion} portal=${buildInfo.portalRevision} git=${buildInfo.gitCommit}`);

writeFileSync(
  join(target, "DO_NOT_EDIT.txt"),
  [
    "This directory is GENERATED by npm run build:esp32",
    "",
    "Do not edit manually.",
    "",
    "Sources:",
    "  Admin  → Vite dist/",
    "  Portal → portal/ (required HTML/JS/CSS for setup/recovery only)",
    "  Production captive portal media → deployment/mikrotik-hotspot/ (MikroTik)",
    "",
    "Regenerate: npm run build:esp32",
    "Deploy:     npm run deploy:esp32",
    "",
  ].join("\n"),
);

// ── 6. Validate ─────────────────────────────────────────────────────────────

assertSpiffsSafePaths(target);
printPortalValidation();
printAdminValidation();

log("Ready for uploadfs");
log(`  cd ESP32_S3_Firmware && pio run -t uploadfs`);
log(`  — or — npm run deploy:esp32`);
