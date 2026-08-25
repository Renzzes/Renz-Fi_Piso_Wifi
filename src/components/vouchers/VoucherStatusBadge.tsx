import { cn } from "@/lib/utils";
import { voucherStatus } from "@/lib/voucherDisplay";
import type { Voucher } from "@/types/api";

const STATUS_CLASS: Record<string, string> = {
  unused: "bg-blue-500/15 text-blue-700 border-blue-500/25 dark:text-blue-300",
  redeeming: "bg-amber-500/15 text-amber-800 border-amber-500/25 dark:text-amber-300",
  active: "bg-emerald-500/15 text-emerald-700 border-emerald-500/25 dark:text-emerald-300",
  expired: "bg-red-500/15 text-red-700 border-red-500/25 dark:text-red-400",
  disabled: "bg-slate-500/15 text-slate-600 border-slate-500/25 dark:text-slate-300",
  archived: "bg-violet-500/15 text-violet-700 border-violet-500/25 dark:text-violet-300",
};

export function VoucherStatusBadge({ voucher }: { voucher: Voucher }) {
  const status = voucherStatus(voucher);
  return (
    <span
      className={cn(
        "inline-flex items-center rounded-full border px-2 py-0.5 text-[11px] font-medium capitalize",
        STATUS_CLASS[status] ?? "bg-muted text-muted-foreground border-border",
      )}
    >
      {voucher.status || status || "unknown"}
    </span>
  );
}
