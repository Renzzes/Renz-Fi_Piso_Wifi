#!/usr/bin/env node
/**
 * Static contract check for hap-lite-to-hex-lite-migration.rsc
 * RouterOS 7.18.x: migration script must not use :local/:set (local immutability).
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const target = path.join(root, "hap-lite-to-hex-lite-migration.rsc");
const text = fs.readFileSync(target, "utf8");
const lines = text.split(/\r?\n/);

const errors = [];
for (let i = 0; i < lines.length; i++) {
  const line = lines[i];
  const n = i + 1;
  const t = line.trim();
  if (/^:local\s/.test(t)) errors.push(`${n}: forbidden :local in migration script`);
  if (/^:set\s/.test(t)) errors.push(`${n}: forbidden :set in migration script`);
  if (!t.startsWith("#") && /\bwlan1\b/.test(t)) errors.push(`${n}: wlan1 reference outside comment`);
  if (/\/interface wireless/.test(t)) errors.push(`${n}: wireless command`);
  if (!t.startsWith("#") && /\/system reset-configuration/.test(t)) errors.push(`${n}: reset-configuration`);
}

if (errors.length) {
  console.error("routeros-migration-script-contract-check: FAIL");
  for (const e of errors) console.error("  " + e);
  process.exit(1);
}

console.log("routeros-migration-script-contract-check: PASS");
console.log(`  file=${target}`);
console.log("  :local/:set absent, no wlan1/wireless/reset-configuration");
