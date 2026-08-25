import { format } from "date-fns";

export function formatMb(value: number | undefined): string {
  if (value === undefined || !Number.isFinite(value)) return "N/A";
  return value.toFixed(1);
}

export function formatStorageFromMb(mb: number | undefined): string {
  if (mb === undefined || !Number.isFinite(mb)) return "N/A";
  if (Math.abs(mb) >= 1024) return `${(mb / 1024).toFixed(1)} GB`;
  return `${mb.toFixed(1)} MB`;
}

export function formatKb(value: number | undefined): string {
  if (value === undefined || !Number.isFinite(value)) return "N/A";
  return `${Math.round(value).toLocaleString()} KB`;
}

export function formatPercentage(value: number | undefined): string {
  if (value === undefined || !Number.isFinite(value)) return "N/A";
  return `${Math.round(value)}%`;
}

export function usagePct(used: number, total: number): number {
  if (!Number.isFinite(used) || !Number.isFinite(total) || total <= 0) return 0;
  return Math.min(100, Math.max(0, Math.round((used / total) * 100)));
}

export function clampPct(value: number | undefined): number {
  if (value === undefined || !Number.isFinite(value)) return 0;
  return Math.min(100, Math.max(0, value));
}

export function formatSessions(sessions: number | undefined, loading: boolean): string {
  if (loading) return "Loading...";
  if (sessions === undefined) return "N/A";
  return `${sessions} sessions`;
}

export function formatTimeOfDay(value?: string | null): string {
  if (!value) return "N/A";
  const ms = Date.parse(value);
  if (!Number.isFinite(ms)) return value;
  return format(new Date(ms), "hh:mm a");
}

export function formatTimestamp(value?: string | null): string {
  if (!value) return "N/A";
  const ms = Date.parse(value);
  if (!Number.isFinite(ms)) return value;
  return format(new Date(ms), "yyyy-MM-dd HH:mm:ss");
}

export function formatRouterMemory(freeBytes?: string, totalBytes?: string): string {
  const free = Number(freeBytes);
  const total = Number(totalBytes);
  if (!Number.isFinite(free) || !Number.isFinite(total) || total <= 0) return "N/A";
  const usedMb = (total - free) / 1024 / 1024;
  const totalMb = total / 1024 / 1024;
  return `${usedMb.toFixed(0)} / ${totalMb.toFixed(0)} MB`;
}
