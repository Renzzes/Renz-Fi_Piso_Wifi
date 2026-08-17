/**
 * @deprecated Use scripts/stage-esp32-data.mjs via npm run build:esp32
 */
import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

console.warn("[build:esp32] copy-dist-to-esp32.mjs is deprecated — use stage-esp32-data.mjs");

const script = join(dirname(fileURLToPath(import.meta.url)), "stage-esp32-data.mjs");
const result = spawnSync(process.execPath, [script], { stdio: "inherit" });
process.exit(result.status ?? 1);
