/**
 * Generates PWA PNG icons from public/favicon.svg.
 * Run: node scripts/generate-pwa-icons.mjs
 */
import { mkdirSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import sharp from "sharp";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const svgPath = join(root, "public", "favicon.svg");
const outDir = join(root, "public", "icons");

mkdirSync(outDir, { recursive: true });

const svg = readFileSync(svgPath);

async function writeIcon(size, filename, paddingRatio = 0) {
  const pad = Math.round(size * paddingRatio);
  const inner = size - pad * 2;
  const png = await sharp(svg)
    .resize(inner, inner, { fit: "contain", background: { r: 30, g: 41, b: 59, alpha: 1 } })
    .extend({
      top: pad,
      bottom: pad,
      left: pad,
      right: pad,
      background: { r: 30, g: 41, b: 59, alpha: 1 },
    })
    .png()
    .toBuffer();

  await sharp(png).toFile(join(outDir, filename));
}

await writeIcon(192, "icon-192.png");
await writeIcon(512, "icon-512.png");
// Maskable safe zone: ~20% padding on each side (content in center 60–80%).
await writeIcon(512, "icon-512-maskable.png", 0.1);

console.log("[pwa-icons] Wrote public/icons/icon-192.png");
console.log("[pwa-icons] Wrote public/icons/icon-512.png");
console.log("[pwa-icons] Wrote public/icons/icon-512-maskable.png");
