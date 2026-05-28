import { db } from "../db/connection.js";
import { writeQueue } from "./writeQueue.js";

export type LogLevel = "DEBUG" | "INFO" | "OK" | "WARN" | "ERR";
export type LogCategory =
  | "auth"
  | "sync"
  | "mikrotik"
  | "vouchers"
  | "diagnostics"
  | "captive_portal"
  | "firmware"
  | "system";

export type StructuredLogInput = {
  level: LogLevel;
  category: LogCategory;
  message: string;
  metadata?: Record<string, unknown>;
  requestId?: string;
  correlationId?: string;
};

function levelToDb(level: LogLevel) {
  if (level === "DEBUG") return "INFO";
  if (level === "OK") return "OK";
  return level;
}

export function logStructured(input: StructuredLogInput) {
  const meta = {
    ...(input.metadata ?? {}),
    ...(input.requestId ? { requestId: input.requestId } : {}),
    ...(input.correlationId ? { correlationId: input.correlationId } : {}),
  };
  const metadataJson = Object.keys(meta).length ? JSON.stringify(meta) : null;

  writeQueue.enqueue("logs", () => {
    db.prepare(
      `INSERT INTO logs (level, message, type, metadata, created_at)
       VALUES (?, ?, ?, ?, datetime('now'))`,
    ).run(levelToDb(input.level), input.message, input.category, metadataJson);
  });

  if (process.env.NODE_ENV !== "production" && input.level === "ERR") {
    console.error(`[${input.category}]`, input.message, meta);
  }
}

export function cleanupOldLogs(retentionDays: number) {
  writeQueue.enqueue("logs", () => {
    db.prepare(`DELETE FROM logs WHERE datetime(created_at) < datetime('now', ?)`).run(
      `-${retentionDays} days`,
    );
  });
}
