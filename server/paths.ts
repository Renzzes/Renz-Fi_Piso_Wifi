import path from "node:path";
import { fileURLToPath } from "node:url";

const isBrowser = typeof window !== "undefined";

export function getProjectRoot() {
  if (isBrowser) return "";
  return process.cwd();
}

export function getDataDir(envDataDir?: string) {
  return envDataDir ?? path.join(getProjectRoot(), "data");
}

export function getDbFilePath(dataDir: string, envDbPath?: string) {
  return envDbPath ?? path.join(dataDir, "renz-fi.db");
}

export function getDistPath(serverDir: string) {
  return path.join(path.dirname(serverDir), "..", "dist");
}

export function resolveFromImportMeta(metaUrl: string, ...segments: string[]) {
  return path.join(path.dirname(fileURLToPath(metaUrl)), ...segments);
}

export const runtime = {
  isBrowser,
  isNode: !isBrowser,
  isElectron: !isBrowser && Boolean(process.versions.electron),
};
