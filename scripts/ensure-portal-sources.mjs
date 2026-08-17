/**
 * Ensures portal/ contains all source files required for ESP32 staging.
 * Generates favicon.ico and Default-Banner.png from public/favicon.svg when absent.
 */
import { existsSync, mkdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import sharp from "sharp";
import {
  PORTAL_RECOMMENDED,
  PORTAL_REQUIRED,
  PORTAL_SOURCE_DIR,
} from "./esp32-staging-manifest.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const portalDir = join(root, PORTAL_SOURCE_DIR);
const svgPath = join(root, "public", "favicon.svg");

function fail(message) {
  console.error(`[portal] ERROR: ${message}`);
  process.exit(1);
}

export async function ensurePortalSources() {
  mkdirSync(portalDir, { recursive: true });

  if (!existsSync(svgPath)) {
    fail(`Missing ${svgPath} — cannot generate favicon.ico / Default-Banner.png`);
  }

  const svg = readFileSync(svgPath);
  const faviconPath = join(portalDir, "favicon.ico");
  const bannerPath = join(portalDir, "Default-Banner.png");

  if (!existsSync(faviconPath)) {
    await sharp(svg)
      .resize(32, 32, { fit: "contain", background: { r: 11, g: 132, b: 255, alpha: 1 } })
      .png()
      .toFile(faviconPath);
    console.log("[portal] Generated favicon.ico from public/favicon.svg");
  }

  if (!existsSync(bannerPath)) {
    await sharp(svg)
      .resize(320, 120, { fit: "contain", background: { r: 15, g: 23, b: 42, alpha: 1 } })
      .extend({
        top: 40,
        bottom: 40,
        left: 240,
        right: 240,
        background: { r: 15, g: 23, b: 42, alpha: 1 },
      })
      .png()
      .toFile(bannerPath);
    console.log("[portal] Generated Default-Banner.png from public/favicon.svg");
  }

  const missingRequired = PORTAL_REQUIRED.filter(
    ({ source }) => !existsSync(join(portalDir, source)),
  ).map(({ label }) => label);

  if (missingRequired.length > 0) {
    fail(
      `Missing required portal source file(s) in ${PORTAL_SOURCE_DIR}/:\n` +
        missingRequired.map((name) => `  - ${name}`).join("\n") +
        `\n\nAdd or restore sources under ${PORTAL_SOURCE_DIR}/ before staging.`,
    );
  }

  const missingRecommended = PORTAL_RECOMMENDED.filter(
    ({ source }) => !existsSync(join(portalDir, source)),
  );

  if (missingRecommended.length > 0) {
    console.warn("[portal] WARN: optional portal assets missing:");
    for (const { label } of missingRecommended) {
      console.warn(`[portal]   - ${label}`);
    }
  }

  return portalDir;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  await ensurePortalSources();
  console.log("[portal] Source directory OK");
}
