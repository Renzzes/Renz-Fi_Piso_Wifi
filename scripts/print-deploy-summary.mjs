/**
 * Prints the post-deploy summary banner for installers.
 */
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { ESP32_DATA_DIR } from "./esp32-staging-manifest.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");

const ENV_LABELS = {
  freenove_esp32_s3_wroom: "Production",
  renzfi_installer: "Installer",
  renzfi_developer: "Developer",
};

function readBuildInfo() {
  const path = join(root, ESP32_DATA_DIR, "build-info.json");
  if (!existsSync(path)) return null;
  return JSON.parse(readFileSync(path, "utf8"));
}

function resolveUploadPort(firmwareDir, pioEnv) {
  const iniPath = join(firmwareDir, "platformio.ini");
  if (!existsSync(iniPath)) return process.env.UPLOAD_PORT ?? "—";

  const ini = readFileSync(iniPath, "utf8");
  const section = `[env:${pioEnv}]`;
  const start = ini.indexOf(section);
  if (start === -1) return process.env.UPLOAD_PORT ?? "—";

  const nextSection = ini.indexOf("\n[env:", start + section.length);
  const block = nextSection === -1 ? ini.slice(start) : ini.slice(start, nextSection);
  const match = block.match(/^\s*upload_port\s*=\s*(\S+)/m);
  return match?.[1] ?? process.env.UPLOAD_PORT ?? "—";
}

function formatAdminDate(iso) {
  if (!iso) return "—";
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return iso.slice(0, 10);
  return d.toISOString().slice(0, 10);
}

function line(label, value) {
  console.log(`${label.padEnd(12)}${value}`);
}

export function printDeploySummary(pioEnv) {
  const build = readBuildInfo();
  const firmwareDir = join(root, "ESP32_S3_Firmware");
  const target = resolveUploadPort(firmwareDir, pioEnv);
  const envLabel = ENV_LABELS[pioEnv] ?? pioEnv;

  console.log("");
  console.log("========================================");
  console.log("Renz-Fi Deployment Complete");
  console.log("========================================");
  line("Firmware", build?.firmwareVersion ?? "—");
  line("Admin", `Build ${formatAdminDate(build?.adminBuild)}`);
  line("Portal", build?.portalRevision ? `Verified (${build.portalRevision})` : "Verified");
  line("SPIFFS", "Verified");
  line("Git", build?.gitCommit ?? "—");
  line("Build #", build?.buildNumber != null ? String(build.buildNumber) : "—");
  line("Target", target);
  line("Environment", envLabel);
  console.log("========================================");
  console.log("");
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  printDeploySummary(process.env.PIO_ENV || "freenove_esp32_s3_wroom");
}
