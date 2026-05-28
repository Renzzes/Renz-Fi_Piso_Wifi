import fs from "node:fs";
import path from "node:path";
import { db, getDbPath } from "../db/connection.js";
import { config } from "../config/index.js";
import { writeQueue } from "./writeQueue.js";

let lastBackupAt: string | null = null;
let lastIntegrityOk = true;

export function runIntegrityCheck() {
  try {
    const row = db.pragma("integrity_check", { simple: true }) as unknown;
    const ok = row === "ok" || (Array.isArray(row) && row[0] === "ok");
    lastIntegrityOk = ok;
    return { ok, detail: row };
  } catch (err) {
    lastIntegrityOk = false;
    return { ok: false, detail: err instanceof Error ? err.message : String(err) };
  }
}

export function getDbSizeBytes() {
  try {
    return fs.statSync(getDbPath()).size;
  } catch {
    return 0;
  }
}

export function getDbHealth() {
  let pingOk = true;
  try {
    db.prepare("SELECT 1").get();
  } catch {
    pingOk = false;
  }

  return {
    ok: pingOk && lastIntegrityOk,
    path: getDbPath(),
    sizeBytes: getDbSizeBytes(),
    walMode: String(db.pragma("journal_mode", { simple: true })),
    integrity: lastIntegrityOk,
    lastBackupAt,
  };
}

function rotateBackups(backupDir: string, keep: number) {
  if (!fs.existsSync(backupDir)) return;
  const files = fs
    .readdirSync(backupDir)
    .filter((f) => f.endsWith(".db"))
    .map((f) => ({ name: f, mtime: fs.statSync(path.join(backupDir, f)).mtimeMs }))
    .sort((a, b) => b.mtime - a.mtime);
  for (const old of files.slice(keep)) {
    fs.unlinkSync(path.join(backupDir, old.name));
  }
}

export async function createTimestampedBackup() {
  const backupDir = path.join(config.dataDir, "backups");
  if (!fs.existsSync(backupDir)) fs.mkdirSync(backupDir, { recursive: true });

  const stamp = new Date().toISOString().replace(/[:.]/g, "-");
  const dest = path.join(backupDir, `renz-fi-${stamp}.db`);

  await writeQueue.flush();

  try {
    await db.backup(dest);
  } catch {
    db.pragma("wal_checkpoint(TRUNCATE)");
    fs.copyFileSync(getDbPath(), dest);
  }

  lastBackupAt = new Date().toISOString();
  rotateBackups(backupDir, config.db.backupRetention);
  return { path: dest, createdAt: lastBackupAt };
}

export function scheduleAutoBackup() {
  const intervalMs = config.db.autoBackupHours * 60 * 60 * 1000;
  setInterval(() => {
    void createTimestampedBackup().catch((err) => {
      console.error("[dbHealth] auto backup failed", err);
    });
  }, intervalMs).unref();
}
