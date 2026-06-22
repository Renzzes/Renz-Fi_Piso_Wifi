import {
  cpSync,
  existsSync,
  mkdirSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { join } from "node:path";
import { dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const dist = join(root, "dist");
const target = join(root, "ESP32_S3_Firmware", "data");
const distAssets = join(dist, "assets");
const targetAssets = join(target, "assets");

// ── Pre-flight checks ─────────────────────────────────────────────────────────

if (!existsSync(dist)) {
  console.error("[build:esp32] ERROR: dist/ not found. Run npm run build first.");
  process.exit(1);
}

if (!existsSync(join(dist, "sw.js"))) {
  console.error("[build:esp32] ERROR: dist/sw.js not found. PWA service worker will not install.");
  process.exit(1);
}

if (!existsSync(join(dist, "index.html"))) {
  console.error("[build:esp32] ERROR: dist/index.html not found.");
  process.exit(1);
}

if (!existsSync(distAssets)) {
  console.error(
    "[build:esp32] ERROR: dist/assets/ not found. Check vite.config.ts assetsDir / rollup output.",
  );
  process.exit(1);
}

const assetFiles = readdirSync(distAssets).filter((name) =>
  statSync(join(distAssets, name)).isFile(),
);
const hasJs = assetFiles.some((name) => name.endsWith(".js"));
const hasCss = assetFiles.some((name) => name.endsWith(".css"));

if (!hasJs || !hasCss) {
  console.error("[build:esp32] ERROR: dist/assets/ must contain built .js and .css bundles.");
  console.error("[build:esp32]   Found:", assetFiles.join(", ") || "(empty)");
  process.exit(1);
}

// ── Clean target ──────────────────────────────────────────────────────────────

if (existsSync(target)) {
  rmSync(target, { recursive: true, force: true });
}
mkdirSync(target, { recursive: true });

// ── Copy only the approved root-level files ───────────────────────────────────
// Never blindly copy all of dist/ — that would include Logo.png, logo2.png and
// any other public/ blobs that must NOT waste precious SPIFFS space.

const rootFiles = ["index.html", "favicon.svg", "sw.js", "manifest.webmanifest"];
for (const name of rootFiles) {
  const src = join(dist, name);
  if (existsSync(src)) {
    cpSync(src, join(target, name));
  }
}

// ── Copy PWA icons ────────────────────────────────────────────────────────────

const distIcons = join(dist, "icons");
const targetIcons = join(target, "icons");
if (existsSync(distIcons)) {
  mkdirSync(targetIcons, { recursive: true });
  cpSync(distIcons, targetIcons, { recursive: true });
} else {
  console.warn("[build:esp32] WARN: dist/icons/ not found — run npm run pwa:icons before build");
}

// ── Copy assets/ directory preserving structure ───────────────────────────────

mkdirSync(targetAssets, { recursive: true });
cpSync(distAssets, targetAssets, { recursive: true });

const buildInfo = {
  buildId: new Date().toISOString(),
  copiedAt: new Date().toISOString(),
  source: "npm run build:esp32",
};
writeFileSync(join(target, "build-info.json"), JSON.stringify(buildInfo, null, 2));

// ── Verify: confirm no Vite hashed bundles leaked to data/ root ──────────────
// Vite hashed bundles match: index-<hash>.js / index-<hash>.css (with optional .gz)

const viteBundlePattern = /index-[A-Za-z0-9_-]+\.(js|css)(\.gz)?$/;

const rootLeaks = readdirSync(target).filter((name) => {
  if (!statSync(join(target, name)).isFile()) return false;
  return viteBundlePattern.test(name);
});

if (rootLeaks.length > 0) {
  console.error("[build:esp32] ERROR: Vite bundles found in data/ root (must be in data/assets/):");
  for (const name of rootLeaks) console.error(`[build:esp32]   data/${name}`);
  process.exit(1);
}

// ── Verify: confirm assets are present in data/assets/ ───────────────────────

const copiedAssets = readdirSync(targetAssets).filter((name) =>
  statSync(join(targetAssets, name)).isFile(),
);

if (copiedAssets.length === 0) {
  console.error("[build:esp32] ERROR: data/assets/ is empty after copy.");
  process.exit(1);
}

// ── Success output ────────────────────────────────────────────────────────────

console.log("[build:esp32] data/ root files:");
for (const name of rootFiles) {
  if (existsSync(join(target, name))) {
    console.log(`[build:esp32]   ${name}`);
  }
}

console.log("[build:esp32] data/assets contents:");
for (const name of copiedAssets) {
  console.log(`[build:esp32]   ${name}`);
}

if (existsSync(targetIcons)) {
  console.log("[build:esp32] data/icons contents:");
  for (const name of readdirSync(targetIcons)) {
    if (statSync(join(targetIcons, name)).isFile()) {
      console.log(`[build:esp32]   ${name}`);
    }
  }
}

const jsCount = copiedAssets.filter((n) => n.endsWith(".js") || n.endsWith(".js.gz")).length;
const cssCount = copiedAssets.filter((n) => n.endsWith(".css") || n.endsWith(".css.gz")).length;
console.log(`[build:esp32] OK: ${jsCount} JS bundle(s), ${cssCount} CSS bundle(s) in data/assets/`);
console.log("[build:esp32] /assets/* present: yes");
console.log(
  "[build:esp32] Next: cd ESP32_S3_Firmware && pio run -t uploadfs (then pio run -t upload if firmware changed)",
);
