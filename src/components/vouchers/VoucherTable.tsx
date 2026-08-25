import { Archive, Eye, Loader2, Trash2 } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import { Skeleton } from "@/components/ui/skeleton";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import { VoucherStatusBadge } from "@/components/vouchers/VoucherStatusBadge";
import {
  formatVoucherAmount,
  formatVoucherDuration,
  formatVoucherExpires,
  isDeletableStatus,
  voucherSpeedLabel,
  voucherStatus,
} from "@/lib/voucherDisplay";
import { cn } from "@/lib/utils";
import type { Voucher } from "@/types/api";

export function VoucherTable({
  rows,
  loading,
  selected,
  onToggle,
  onTogglePage,
  onView,
  onDelete,
  onAction,
  actionPending,
  deletePending,
}: {
  rows: Voucher[];
  loading: boolean;
  selected: Record<string, boolean>;
  onToggle: (code: string, checked: boolean) => void;
  onTogglePage: (checked: boolean) => void;
  onView: (voucher: Voucher) => void;
  onDelete: (voucher: Voucher) => void;
  onAction: (code: string, action: "terminate" | "disable" | "archive") => void;
  actionPending: boolean;
  deletePending: boolean;
}) {
  const selectedOnPage = rows.filter((v) => selected[v.code]);
  const allSelected = rows.length > 0 && selectedOnPage.length === rows.length;
  const someSelected = selectedOnPage.length > 0 && !allSelected;

  return (
    <Table>
      <TableHeader>
        <TableRow className="hover:bg-transparent">
          <TableHead className="h-10 w-10 bg-muted/40">
            <Checkbox
              aria-label="Select page"
              checked={someSelected ? "indeterminate" : allSelected}
              onCheckedChange={(value) => onTogglePage(value === true)}
              disabled={loading || rows.length === 0}
            />
          </TableHead>
          <TableHead className="h-10 bg-muted/40 text-[12px]">Code</TableHead>
          <TableHead className="h-10 bg-muted/40 text-[12px]">Amount</TableHead>
          <TableHead className="h-10 bg-muted/40 text-[12px]">Time</TableHead>
          <TableHead className="h-10 bg-muted/40 text-[12px]">Status</TableHead>
          <TableHead className="h-10 bg-muted/40 text-[12px]">Expires</TableHead>
          <TableHead className="h-10 bg-muted/40 text-[12px]">Bound Device</TableHead>
          <TableHead className="h-10 bg-muted/40 text-[12px]">Speed</TableHead>
          <TableHead className="h-10 bg-muted/40 text-right text-[12px]">Actions</TableHead>
        </TableRow>
      </TableHeader>
      <TableBody>
        {loading
          ? Array.from({ length: 12 }).map((_, index) => (
              <TableRow key={`sk-${index}`} className="h-12">
                {Array.from({ length: 9 }).map((__, cell) => (
                  <TableCell key={cell}>
                    <Skeleton className="h-3.5 w-full max-w-[7rem]" />
                  </TableCell>
                ))}
              </TableRow>
            ))
          : rows.map((v) => {
              const status = voucherStatus(v);
              const deletable = isDeletableStatus(status);
              const checked = Boolean(selected[v.code]);
              return (
                <TableRow
                  key={v.code}
                  className={cn("h-12", checked && "bg-primary/5")}
                  data-state={checked ? "selected" : undefined}
                >
                  <TableCell>
                    <Checkbox
                      aria-label={`Select ${v.code}`}
                      checked={checked}
                      onCheckedChange={(value) => onToggle(v.code, value === true)}
                    />
                  </TableCell>
                  <TableCell className="font-mono text-[12px] font-medium tracking-wide text-foreground">
                    {v.code}
                  </TableCell>
                  <TableCell className="text-[13px] tabular-nums">
                    {formatVoucherAmount(v.amount)}
                  </TableCell>
                  <TableCell className="text-[13px] tabular-nums">
                    {formatVoucherDuration(v.minutes)}
                  </TableCell>
                  <TableCell>
                    <VoucherStatusBadge voucher={v} />
                  </TableCell>
                  <TableCell className="font-mono text-[11px] text-muted-foreground">
                    {formatVoucherExpires(v.expires)}
                  </TableCell>
                  <TableCell
                    className="max-w-[140px] truncate font-mono text-[11px] text-muted-foreground"
                    title={v.boundMac || undefined}
                  >
                    {v.boundMac || "-"}
                  </TableCell>
                  <TableCell
                    className="max-w-[120px] truncate text-[12px]"
                    title={voucherSpeedLabel(v)}
                  >
                    {voucherSpeedLabel(v)}
                  </TableCell>
                  <TableCell className="text-right">
                    <div className="flex flex-nowrap items-center justify-end gap-1">
                      <Button
                        type="button"
                        size="sm"
                        variant="outline"
                        className="h-7 px-2 text-[11px]"
                        onClick={() => onView(v)}
                      >
                        <Eye className="h-3.5 w-3.5" />
                        View
                      </Button>
                      {(status === "active" || status === "redeeming") && (
                        <Button
                          type="button"
                          size="sm"
                          variant="destructive"
                          className="h-7 px-2 text-[11px]"
                          disabled={actionPending}
                          onClick={() => onAction(v.code, "terminate")}
                        >
                          Terminate
                        </Button>
                      )}
                      {status === "unused" && (
                        <Button
                          type="button"
                          size="sm"
                          variant="outline"
                          className="h-7 px-2 text-[11px]"
                          disabled={actionPending}
                          onClick={() => onAction(v.code, "disable")}
                        >
                          Disable
                        </Button>
                      )}
                      {(status === "unused" || status === "expired" || status === "disabled") && (
                        <Button
                          type="button"
                          size="sm"
                          variant="outline"
                          className="h-7 px-2 text-[11px]"
                          disabled={actionPending}
                          onClick={() => onAction(v.code, "archive")}
                        >
                          <Archive className="h-3.5 w-3.5" />
                          Archive
                        </Button>
                      )}
                      {deletable && (
                        <Button
                          type="button"
                          size="sm"
                          variant="outline"
                          className="h-7 px-2 text-[11px] border-red-500/40 bg-red-500/10 text-red-600 hover:bg-red-500/20 dark:text-red-400"
                          disabled={deletePending}
                          onClick={() => onDelete(v)}
                        >
                          {deletePending ? (
                            <Loader2 className="h-3.5 w-3.5 animate-spin" />
                          ) : (
                            <Trash2 className="h-3.5 w-3.5" />
                          )}
                          Delete
                        </Button>
                      )}
                    </div>
                  </TableCell>
                </TableRow>
              );
            })}
      </TableBody>
    </Table>
  );
}
