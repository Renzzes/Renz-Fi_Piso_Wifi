import { publishAdminEvent } from "./eventBus.js";

export type RamLogEntry = {
  id: number;
  t: string;
  lvl: string;
  type: string;
  msg: string;
};

const MAX = 500;
const buffer: RamLogEntry[] = [];
let nextId = 1;

export function pushRamLog(entry: Omit<RamLogEntry, "id" | "t"> & { t?: string }) {
  const row: RamLogEntry = {
    id: nextId++,
    t: entry.t ?? new Date().toISOString(),
    lvl: entry.lvl,
    type: entry.type,
    msg: entry.msg,
  };
  buffer.push(row);
  if (buffer.length > MAX) buffer.splice(0, buffer.length - MAX);
  publishAdminEvent("log.entry", row as unknown as Record<string, unknown>);
  publishAdminEvent("logs.changed");
  return row;
}

export function listRamLogs(q?: string) {
  const needle = String(q ?? "").toLowerCase();
  if (!needle) return [...buffer];
  return buffer.filter(
    (l) =>
      l.msg.toLowerCase().includes(needle) ||
      l.type.toLowerCase().includes(needle) ||
      l.lvl.toLowerCase().includes(needle),
  );
}

export function clearRamLogs() {
  buffer.length = 0;
  publishAdminEvent("logs.changed");
}

export function exportRamLogs() {
  return JSON.stringify(buffer, null, 2);
}

export function seedRamLogsFromDb(rows: RamLogEntry[]) {
  buffer.length = 0;
  for (const row of rows.slice(-MAX)) buffer.push(row);
}
