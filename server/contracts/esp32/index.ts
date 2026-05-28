import { z } from "zod";

export const esp32EventTypes = [
  "coin_insert",
  "voucher_activate",
  "heartbeat",
  "diagnostics",
  "sync_batch",
  "uptime",
  "storage_status",
] as const;

export type Esp32EventType = (typeof esp32EventTypes)[number];

const isoTimestamp = z
  .string()
  .datetime({ offset: true })
  .or(z.string().regex(/^\d{4}-\d{2}-\d{2}/));

export const coinInsertSchema = z.object({
  type: z.literal("coin_insert"),
  deviceId: z.string().min(1).max(64),
  amount: z.number().int().positive(),
  pulseCount: z.number().int().nonnegative().optional(),
  recordedAt: isoTimestamp,
  idempotencyKey: z.string().min(1).max(128),
});

export const voucherActivateSchema = z.object({
  type: z.literal("voucher_activate"),
  deviceId: z.string().min(1).max(64),
  code: z.string().min(1).max(32),
  clientIp: z.string().optional(),
  recordedAt: isoTimestamp,
  idempotencyKey: z.string().min(1).max(128),
});

export const heartbeatSchema = z.object({
  type: z.literal("heartbeat"),
  deviceId: z.string().min(1).max(64),
  uptimeSeconds: z.number().int().nonnegative(),
  recordedAt: isoTimestamp,
  idempotencyKey: z.string().min(1).max(128),
});

export const diagnosticsSchema = z.object({
  type: z.literal("diagnostics"),
  deviceId: z.string().min(1).max(64),
  state: z.string(),
  errors: z.number().int().nonnegative().optional(),
  metadata: z.record(z.unknown()).optional(),
  recordedAt: isoTimestamp,
  idempotencyKey: z.string().min(1).max(128),
});

export const syncBatchSchema = z.object({
  type: z.literal("sync_batch"),
  deviceId: z.string().min(1).max(64),
  batchId: z.string().min(1).max(64),
  items: z.array(z.record(z.unknown())),
  recordedAt: isoTimestamp,
  idempotencyKey: z.string().min(1).max(128),
});

export const uptimeSchema = z.object({
  type: z.literal("uptime"),
  deviceId: z.string().min(1).max(64),
  uptimeSeconds: z.number().int().nonnegative(),
  recordedAt: isoTimestamp,
  idempotencyKey: z.string().min(1).max(128),
});

export const storageStatusSchema = z.object({
  type: z.literal("storage_status"),
  deviceId: z.string().min(1).max(64),
  flashUsedMb: z.number().nonnegative(),
  flashTotalMb: z.number().positive(),
  ramUsedKb: z.number().nonnegative(),
  ramTotalKb: z.number().positive(),
  recordedAt: isoTimestamp,
  idempotencyKey: z.string().min(1).max(128),
});

export const esp32PayloadSchema = z.discriminatedUnion("type", [
  coinInsertSchema,
  voucherActivateSchema,
  heartbeatSchema,
  diagnosticsSchema,
  syncBatchSchema,
  uptimeSchema,
  storageStatusSchema,
]);

export type Esp32Payload = z.infer<typeof esp32PayloadSchema>;

export function normalizeEsp32Timestamp(ts: string) {
  const d = new Date(ts);
  return Number.isNaN(d.getTime()) ? new Date().toISOString() : d.toISOString();
}

export function buildDedupeKey(deviceId: string, idempotencyKey: string) {
  return `${deviceId}:${idempotencyKey}`;
}

export interface Esp32SyncService {
  ingest(payload: Esp32Payload): Promise<{ accepted: boolean; duplicate?: boolean }>;
}
