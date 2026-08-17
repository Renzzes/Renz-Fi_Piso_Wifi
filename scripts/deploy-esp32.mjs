/**
 * Full ESP32 deployment: stage assets → build firmware → upload → upload SPIFFS
 */
import { spawnSync } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { printDeploySummary } from "./print-deploy-summary.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const firmwareDir = join(root, "ESP32_S3_Firmware");

const PIO_ENV = process.env.PIO_ENV || "freenove_esp32_s3_wroom";

function run(command, args, cwd) {
  console.log(`\n[deploy:esp32] > ${command} ${args.join(" ")}`);
  const result = spawnSync(command, args, { cwd, stdio: "inherit", shell: true });
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

function resolvePio() {
  for (const cmd of ["pio", "platformio"]) {
    const check = spawnSync(cmd, ["--version"], { shell: true, encoding: "utf8" });
    if (check.status === 0) return cmd;
  }
  console.error("[deploy:esp32] ERROR: PlatformIO CLI not found (pio / platformio)");
  console.error("[deploy:esp32] Install PlatformIO, then re-run npm run deploy:esp32");
  process.exit(1);
}

const pio = resolvePio();

console.log(`[deploy:esp32] PlatformIO environment: ${PIO_ENV}`);
console.log("[deploy:esp32] data/ was staged by npm run build:esp32 (same command chain)");

run(pio, ["run", "-e", PIO_ENV], firmwareDir);
run(pio, ["run", "-e", PIO_ENV, "-t", "upload"], firmwareDir);
run(pio, ["run", "-e", PIO_ENV, "-t", "uploadfs"], firmwareDir);

printDeploySummary(PIO_ENV);
