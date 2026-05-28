import Database from "better-sqlite3";
import fs from "node:fs";
import { config } from "../config/index.js";
import { getDbFilePath } from "../paths.js";

const dataDir = config.dataDir;
const dbPath = getDbFilePath(dataDir, process.env.DB_PATH);

if (!fs.existsSync(dataDir)) {
  fs.mkdirSync(dataDir, { recursive: true });
}

export const db = new Database(dbPath);

db.pragma(`journal_mode = WAL`);
db.pragma(`foreign_keys = ON`);
db.pragma(`busy_timeout = ${config.db.busyTimeoutMs}`);
db.pragma(`synchronous = NORMAL`);
db.pragma(`temp_store = MEMORY`);

export function getDbPath() {
  return dbPath;
}
