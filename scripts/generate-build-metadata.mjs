/**
 * Generates build-info.json for ESP32 SPIFFS staging.
 */
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { CONTRACT_VERSIONS } from "./contract-versions.mjs";
import { PORTAL_REQUIRED, PORTAL_SOURCE_DIR } from "./esp32-staging-manifest.mjs";

const scriptRoot = join(dirname(fileURLToPath(import.meta.url)), "..");

function git(args) {
  const result = spawnSync("git", args, {
    cwd: scriptRoot,
    encoding: "utf8",
    shell: process.platform === "win32",
  });
  if (result.status !== 0) return null;
  return (result.stdout ?? "").trim() || null;
}

function readFirmwareVersion(configPath) {
  if (!existsSync(configPath)) return "unknown";
  const match = readFileSync(configPath, "utf8").match(
    /FIRMWARE_VERSION\s*=\s*"([^"]+)"/,
  );
  return match?.[1] ?? "unknown";
}

function readAdminBuild(distDir) {
  const adminBuildFile = join(distDir, "admin-build.json");
  if (existsSync(adminBuildFile)) {
    try {
      const parsed = JSON.parse(readFileSync(adminBuildFile, "utf8"));
      if (parsed.adminBuild) return parsed.adminBuild;
    } catch {
      /* fall through */
    }
  }
  return new Date().toISOString();
}

function computePortalRevision(portalDir) {
  const hash = createHash("sha256");
  for (const { source } of PORTAL_REQUIRED) {
    const path = join(portalDir, source);
    if (!existsSync(path)) continue;
    hash.update(source);
    hash.update(readFileSync(path));
  }
  return hash.digest("hex").slice(0, 6);
}

/**
 * @param {{ root?: string, distDir?: string, portalDir?: string, stagedAt?: string }} [options]
 */
export function generateBuildMetadata(options = {}) {
  const root = options.root ?? scriptRoot;
  const distDir = options.distDir ?? join(root, "dist");
  const portalDir = options.portalDir ?? join(root, PORTAL_SOURCE_DIR);
  const stagedAt = options.stagedAt ?? new Date().toISOString();

  const gitCommit = git(["rev-parse", "--short=7", "HEAD"]) ?? "unknown";
  const revCount = git(["rev-list", "--count", "HEAD"]);
  const buildNumber = revCount ? Number.parseInt(revCount, 10) : 0;

  return {
    firmwareVersion: readFirmwareVersion(
      join(root, "ESP32_S3_Firmware", "src", "Config.h"),
    ),
    adminBuild: readAdminBuild(distDir),
    portalRevision: computePortalRevision(portalDir),
    gitCommit,
    buildNumber,
    stagedAt,
    ...CONTRACT_VERSIONS,
  };
}
