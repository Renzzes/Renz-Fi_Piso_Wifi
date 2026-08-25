import type { Voucher } from "@/types/api";

export const VOUCHER_STATUSES = [
  "unused",
  "redeeming",
  "active",
  "expired",
  "disabled",
  "archived",
] as const;

export const VOUCHER_PAGE_SIZES = [12, 24, 48] as const;
export const VOUCHER_PAGE_SIZE_DEFAULT = 12;

export function voucherStatus(v: Voucher): string {
  return (v.status ?? "").trim().toLowerCase();
}

export function isDeletableStatus(status: string): boolean {
  return (
    status === "unused" || status === "expired" || status === "disabled" || status === "archived"
  );
}

export function voucherSpeedLabel(v: Voucher): string {
  return v.speed || v.profileName || "Default";
}

export function formatVoucherDuration(minutes: number | undefined): string {
  if (minutes === undefined || !Number.isFinite(minutes)) return "N/A";
  if (minutes >= 60 && minutes % 60 === 0) return `${minutes / 60}hr`;
  return `${minutes}m`;
}

export function formatVoucherExpires(expires: string | undefined): string {
  const value = (expires ?? "").trim();
  if (!value || value.toLowerCase() === "never") return "Never";
  return value;
}

export function formatVoucherAmount(amount: number | undefined): string {
  if (amount === undefined || !Number.isFinite(amount)) return "N/A";
  return `₱${amount}`;
}

export function voucherMatchesQuery(v: Voucher, query: string): boolean {
  const q = query.trim().toLowerCase();
  if (!q) return true;
  const haystack = [
    v.code,
    String(v.amount ?? ""),
    formatVoucherAmount(v.amount),
    voucherStatus(v),
    v.status,
    formatVoucherDuration(v.minutes),
    String(v.minutes ?? ""),
    v.boundMac ?? "",
    voucherSpeedLabel(v),
    v.profileName ?? "",
    v.speed ?? "",
  ]
    .join(" ")
    .toLowerCase();
  return haystack.includes(q);
}

export function filterVouchers(
  vouchers: Voucher[],
  query: string,
  statusFilter: string,
): Voucher[] {
  return vouchers.filter(
    (v) =>
      (statusFilter === "all" || voucherStatus(v) === statusFilter) &&
      voucherMatchesQuery(v, query),
  );
}
