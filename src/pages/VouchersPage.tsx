import { useEffect, useMemo, useRef, useState } from "react";
import { Eye, Plus, Printer, Search } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { NumericInput } from "@/components/NumericInput";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import { Badge } from "@/components/ui/badge";
import {
  Dialog,
  DialogContent,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import {
  useVouchers,
  useGenerateVouchers,
  useVoucherAction,
  useDeleteVoucher,
  useBulkDeleteVouchers,
} from "@/hooks/api/useVouchers";
import { routerApi } from "@/services/router";
import { refreshProductionRouterViews } from "@/lib/refreshProductionRouterViews";
import { toast } from "sonner";
import type { Voucher } from "@/types/api";
import type { VoucherGeneratePayload } from "@/services/vouchers";
import {
  formatDeletedToast,
  formatGeneratedToast,
} from "@/services/vouchers";
import { ApiError } from "@/services/api";

const ROUTER_DEFAULT_PROFILE = "__router_default__";
const SPEED_DEFAULT = "__speed_default__";
const SPEED_CUSTOM = "__speed_custom__";
/** One Generate job creates this many vouchers (inclusive). Default 3. */
const VOUCHER_COUNT_MIN = 1;
const VOUCHER_COUNT_MAX = 20;
const VOUCHER_COUNT_DEFAULT = 3;

function formatRateLimit(rateLimit?: string): string {
  const trimmed = rateLimit?.trim() ?? "";
  return trimmed.length > 0 ? trimmed : "rate-limit not set";
}

function voucherStatus(v: Voucher): string {
  return (v.status ?? "").trim().toLowerCase();
}

function isDeletableStatus(status: string): boolean {
  return (
    status === "unused" ||
    status === "expired" ||
    status === "disabled" ||
    status === "archived"
  );
}

function voucherSpeedLabel(v: Voucher): string {
  return v.speed || v.profileName || "Default";
}

/** Same-page iframe print — never uses window.open / pop-ups. */
function printSelectedVouchers(selected: Voucher[]) {
  if (selected.length === 0) {
    toast.error("Select one or more vouchers to print");
    return;
  }
  const rows = selected
    .map(
      (v) =>
        `<div class="ticket"><div class="code">${v.code}</div>` +
        `<div class="meta">₱${v.amount} · ${v.minutes} min` +
        ` · ${voucherSpeedLabel(v)}` +
        (v.status ? ` · ${v.status}` : "") +
        (v.expires ? ` · redeem before ${v.expires}` : "") +
        `</div></div>`,
    )
    .join("");
  const html = `<!doctype html><html><head><title>Vouchers</title>
<style>
body{font-family:ui-monospace,monospace;padding:16px}
.ticket{border:1px dashed #333;padding:12px;margin:0 0 12px;page-break-inside:avoid}
.code{font-size:22px;font-weight:700;letter-spacing:0.08em}
.meta{font-size:12px;margin-top:6px;color:#333}
@media print{body{padding:0}}
</style></head><body>${rows}</body></html>`;

  const iframe = document.createElement("iframe");
  iframe.setAttribute("aria-hidden", "true");
  iframe.style.cssText =
    "position:fixed;right:0;bottom:0;width:0;height:0;border:0;opacity:0;pointer-events:none";
  document.body.appendChild(iframe);
  const frameDoc = iframe.contentDocument ?? iframe.contentWindow?.document;
  if (!frameDoc) {
    document.body.removeChild(iframe);
    toast.error("Unable to open print preview");
    return;
  }
  frameDoc.open();
  frameDoc.write(html);
  frameDoc.close();

  const cleanup = () => {
    if (iframe.parentNode) iframe.parentNode.removeChild(iframe);
  };
  const runPrint = () => {
    try {
      iframe.contentWindow?.focus();
      iframe.contentWindow?.print();
    } catch {
      toast.error("Unable to open print dialog");
    }
    window.setTimeout(cleanup, 1500);
  };
  window.setTimeout(runPrint, 50);
}

function VoucherPreviewDialog({
  vouchers,
  open,
  onOpenChange,
}: {
  vouchers: Voucher[];
  open: boolean;
  onOpenChange: (open: boolean) => void;
}) {
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-md max-h-[85vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>
            {vouchers.length === 1
              ? "Voucher details"
              : `Preview (${vouchers.length})`}
          </DialogTitle>
        </DialogHeader>
        <div className="space-y-3">
          {vouchers.map((v) => (
            <div
              key={v.code}
              className="rounded-md border border-dashed border-foreground/40 p-3 font-mono"
            >
              <div className="text-xl font-bold tracking-wider">{v.code}</div>
              <div className="mt-2 space-y-1 text-xs text-muted-foreground">
                <div>Amount: ₱{v.amount}</div>
                <div>Validity: {v.minutes} minutes after redeem</div>
                <div>Speed: {voucherSpeedLabel(v)}</div>
                <div>Status: {v.status}</div>
                <div>Redeem before: {v.expires || "never"}</div>
                {v.boundMac ? <div>Bound: {v.boundMac}</div> : null}
              </div>
            </div>
          ))}
        </div>
        <DialogFooter className="gap-2 sm:gap-0">
          <Button variant="outline" onClick={() => onOpenChange(false)}>
            Close
          </Button>
          <Button
            onClick={() => {
              printSelectedVouchers(vouchers);
            }}
          >
            <Printer className="h-4 w-4" /> Print
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

export default function VouchersPage() {
  const { data: vouchers = [] } = useVouchers();
  const generateMutation = useGenerateVouchers();
  const actionMutation = useVoucherAction();
  const deleteMutation = useDeleteVoucher();
  const bulkDeleteMutation = useBulkDeleteVouchers();
  const [q, setQ] = useState("");
  const [filter, setFilter] = useState<string>("all");
  const [open, setOpen] = useState(false);
  const [selected, setSelected] = useState<Record<string, boolean>>({});
  const [preview, setPreview] = useState<Voucher[]>([]);

  const filtered = vouchers.filter(
    (v) =>
      (filter === "all" || voucherStatus(v) === filter) &&
      v.code.toLowerCase().includes(q.toLowerCase()),
  );

  const selectedList = filtered.filter((v) => selected[v.code]);
  const deletableSelected = selectedList.filter((v) =>
    isDeletableStatus(voucherStatus(v)),
  );

  const generate = async (payload: VoucherGeneratePayload) => {
    try {
      if (payload.downloadMbps && payload.uploadMbps) {
        await routerApi.profileOp({
          action: "ensure-managed",
          downloadMbps: payload.downloadMbps,
          uploadMbps: payload.uploadMbps,
        });
      }
      const result = await generateMutation.mutateAsync(payload);
      const n = result.count ?? result.created?.length ?? payload.count;
      toast.success(formatGeneratedToast(n));
      if (import.meta.env.DEV) {
        console.debug(`[voucher-ui] refreshed vouchers after generate count=${n}`);
      }
      setOpen(false);
    } catch (err) {
      const msg =
        err instanceof ApiError
          ? err.message
          : err instanceof Error
            ? err.message
            : "Generate failed";
      toast.error(msg);
    }
  };

  const toggleAll = (on: boolean) => {
    const next: Record<string, boolean> = {};
    if (on) filtered.forEach((v) => (next[v.code] = true));
    setSelected(next);
  };

  return (
    <div>
      <PageHeader
        title="Vouchers"
        description="Generate and manage WiFi vouchers"
        actions={
          <div className="flex gap-2 flex-wrap">
            <Button
              size="sm"
              variant="outline"
              disabled={selectedList.length === 0}
              onClick={() => setPreview(selectedList)}
            >
              <Eye className="h-4 w-4" /> View selected
            </Button>
            <Button
              size="sm"
              variant="destructive"
              disabled={
                deletableSelected.length === 0 || bulkDeleteMutation.isPending
              }
              onClick={() => {
                if (deletableSelected.length === 0) {
                  toast.error("Select at least 1 voucher to delete.");
                  return;
                }
                if (deletableSelected.length > 20) {
                  toast.error(
                    "You can delete a maximum of 20 vouchers at a time.",
                  );
                  return;
                }
                if (
                  !window.confirm(
                    `Delete ${deletableSelected.length} selected voucher(s)? This cannot be undone.`,
                  )
                ) {
                  return;
                }
                bulkDeleteMutation.mutate(
                  deletableSelected.map((v) => v.code),
                  {
                    onSuccess: (result) => {
                      const n =
                        result.count ??
                        result.deleted?.length ??
                        deletableSelected.length;
                      toast.success(formatDeletedToast(n));
                      setSelected({});
                    },
                    onError: (err) =>
                      toast.error(
                        err instanceof Error
                          ? err.message
                          : "Bulk delete failed",
                      ),
                  },
                );
              }}
            >
              {bulkDeleteMutation.isPending ? "Deleting…" : "Delete selected"}
            </Button>
            <p className="w-full text-[11px] text-muted-foreground">
              Select 1–20 vouchers to delete.
            </p>
            <Dialog
              open={open}
              onOpenChange={(v) => !generateMutation.isPending && setOpen(v)}
            >
              <DialogTrigger asChild>
                <Button size="sm" disabled={generateMutation.isPending}>
                  <Plus className="h-4 w-4" />
                  {generateMutation.isPending ? "Generating..." : "Generate"}
                </Button>
              </DialogTrigger>
              <GenerateDialog
                onGenerate={generate}
                generating={generateMutation.isPending}
              />
            </Dialog>
          </div>
        }
      />

      <VoucherPreviewDialog
        vouchers={preview}
        open={preview.length > 0}
        onOpenChange={(next) => {
          if (!next) setPreview([]);
        }}
      />

      <div className="flex flex-wrap gap-2 mb-3">
        <div className="relative flex-1 min-w-[200px]">
          <Search className="absolute left-2 top-1/2 -translate-y-1/2 h-4 w-4 text-muted-foreground" />
          <Input
            className="pl-8 h-8"
            placeholder="Search code"
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />
        </div>
        <Select value={filter} onValueChange={setFilter}>
          <SelectTrigger className="w-[140px] h-8">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="all">All</SelectItem>
            <SelectItem value="unused">Unused</SelectItem>
            <SelectItem value="redeeming">Redeeming</SelectItem>
            <SelectItem value="active">Active</SelectItem>
            <SelectItem value="expired">Expired</SelectItem>
            <SelectItem value="disabled">Disabled</SelectItem>
            <SelectItem value="archived">Archived</SelectItem>
          </SelectContent>
        </Select>
      </div>

      <div className="rounded-md border bg-card overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead className="w-10">
                <input
                  type="checkbox"
                  aria-label="Select all"
                  checked={
                    filtered.length > 0 &&
                    selectedList.length === filtered.length
                  }
                  onChange={(e) => toggleAll(e.target.checked)}
                />
              </TableHead>
              <TableHead>Code</TableHead>
              <TableHead>Amount</TableHead>
              <TableHead>Time</TableHead>
              <TableHead>Status</TableHead>
              <TableHead>Expires</TableHead>
              <TableHead>Bound Device</TableHead>
              <TableHead>Speed</TableHead>
              <TableHead className="text-right min-w-[220px]">Action</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {filtered.map((v) => {
              const status = voucherStatus(v);
              const deletable = isDeletableStatus(status);
              return (
                <TableRow key={v.code}>
                  <TableCell>
                    <input
                      type="checkbox"
                      checked={!!selected[v.code]}
                      onChange={(e) =>
                        setSelected((prev) => ({
                          ...prev,
                          [v.code]: e.target.checked,
                        }))
                      }
                    />
                  </TableCell>
                  <TableCell className="font-mono text-xs">{v.code}</TableCell>
                  <TableCell>₱{v.amount}</TableCell>
                  <TableCell>{v.minutes}m</TableCell>
                  <TableCell>
                    <Badge
                      variant={
                        status === "active"
                          ? "default"
                          : status === "expired"
                            ? "destructive"
                            : "secondary"
                      }
                    >
                      {v.status}
                    </Badge>
                  </TableCell>
                  <TableCell className="text-xs text-muted-foreground">
                    {v.expires}
                  </TableCell>
                  <TableCell className="font-mono text-xs">
                    {v.boundMac || "—"}
                  </TableCell>
                  <TableCell className="text-xs">
                    {voucherSpeedLabel(v)}
                  </TableCell>
                  <TableCell className="text-right whitespace-nowrap">
                    <Button
                      size="sm"
                      variant="ghost"
                      className="h-7"
                      onClick={() => setPreview([v])}
                    >
                      <Eye className="h-3.5 w-3.5" /> View
                    </Button>
                    {(status === "active" || status === "redeeming") && (
                      <Button
                        size="sm"
                        variant="destructive"
                        className="h-7 ml-1"
                        disabled={actionMutation.isPending}
                        onClick={() =>
                          actionMutation.mutate({
                            code: v.code,
                            action: "terminate",
                          })
                        }
                      >
                        Terminate
                      </Button>
                    )}
                    {status === "unused" && (
                      <Button
                        size="sm"
                        variant="outline"
                        className="h-7 ml-1"
                        disabled={actionMutation.isPending}
                        onClick={() =>
                          actionMutation.mutate({
                            code: v.code,
                            action: "disable",
                          })
                        }
                      >
                        Disable
                      </Button>
                    )}
                    {(status === "unused" ||
                      status === "expired" ||
                      status === "disabled") && (
                      <Button
                        size="sm"
                        variant="ghost"
                        className="h-7 ml-1"
                        disabled={actionMutation.isPending}
                        onClick={() =>
                          actionMutation.mutate({
                            code: v.code,
                            action: "archive",
                          })
                        }
                      >
                        Archive
                      </Button>
                    )}
                    {deletable && (
                      <Button
                        size="sm"
                        variant="destructive"
                        className="h-7 ml-1"
                        disabled={deleteMutation.isPending}
                        onClick={() => {
                          if (
                            !window.confirm(
                              `Delete voucher ${v.code}? This cannot be undone.`,
                            )
                          ) {
                            return;
                          }
                          deleteMutation.mutate(v.code, {
                            onSuccess: (result) => {
                              const n = result.count ?? result.deleted?.length ?? 1;
                              toast.success(formatDeletedToast(n));
                            },
                            onError: (err) =>
                              toast.error(
                                err instanceof Error
                                  ? err.message
                                  : "Delete failed",
                              ),
                          });
                        }}
                      >
                        Delete
                      </Button>
                    )}
                  </TableCell>
                </TableRow>
              );
            })}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}

function GenerateDialog({
  onGenerate,
  generating,
}: {
  onGenerate: (payload: VoucherGeneratePayload) => void;
  generating: boolean;
}) {
  const queryClient = useQueryClient();
  const [count, setCount] = useState<number | undefined>(VOUCHER_COUNT_DEFAULT);
  const [amount, setAmount] = useState<number | undefined>(5);
  const [minutes, setMinutes] = useState<number | undefined>(90);
  const [expires, setExpires] = useState("");
  const [profileName, setProfileName] = useState(ROUTER_DEFAULT_PROFILE);
  const [speedMode, setSpeedMode] = useState<string>(SPEED_DEFAULT);
  const [displaySpeed, setDisplaySpeed] = useState("");
  const [customDown, setCustomDown] = useState<number | undefined>(10);
  const [customUp, setCustomUp] = useState<number | undefined>(5);
  const profileRefreshAttempted = useRef(false);

  const { data: profilesData, isLoading: profilesLoading } = useQuery({
    queryKey: ["router", "profiles"],
    queryFn: () => routerApi.profiles(),
    staleTime: Number.POSITIVE_INFINITY,
    refetchOnMount: false,
  });

  const profileOptions = useMemo(
    () => (Array.isArray(profilesData?.profiles) ? profilesData.profiles : []),
    [profilesData?.profiles],
  );

  const profileDetails = useMemo(() => {
    if (
      Array.isArray(profilesData?.profileDetails) &&
      profilesData.profileDetails.length > 0
    ) {
      return profilesData.profileDetails;
    }
    return profileOptions.map((name) => ({ name, rateLimit: "" }));
  }, [profilesData?.profileDetails, profileOptions]);

  useEffect(() => {
    if (profilesLoading || profileRefreshAttempted.current) return;
    if (profileOptions.length > 0) return;
    if (
      profilesData?.error &&
      !String(profilesData.error).includes("unavailable")
    )
      return;
    profileRefreshAttempted.current = true;
    void (async () => {
      try {
        await routerApi.refreshProfiles();
        await refreshProductionRouterViews(queryClient);
      } catch {
        /* keep defaults */
      }
    })();
  }, [profilesLoading, profileOptions.length, profilesData?.error, queryClient]);

  const selectedDetail = profileDetails.find((p) => p.name === profileName);

  return (
    <DialogContent>
      <DialogHeader>
        <DialogTitle>Generate Vouchers</DialogTitle>
      </DialogHeader>
      <div className="grid grid-cols-2 gap-3">
        <div className="space-y-1">
          <Label className="text-xs">Count</Label>
          <NumericInput
            value={count}
            min={VOUCHER_COUNT_MIN}
            max={VOUCHER_COUNT_MAX}
            onValueChange={setCount}
          />
          <p className="text-[11px] text-muted-foreground">
            {VOUCHER_COUNT_MIN}–{VOUCHER_COUNT_MAX} vouchers per Generate job.
          </p>
        </div>
        <div className="space-y-1">
          <Label className="text-xs">Amount (₱)</Label>
          <NumericInput value={amount} min={0} onValueChange={setAmount} />
        </div>
        <div className="space-y-1">
          <Label className="text-xs">Validity (minutes after redeem)</Label>
          <NumericInput
            value={minutes}
            min={1}
            max={525600}
            onValueChange={setMinutes}
          />
          <p className="text-[11px] text-muted-foreground">
            Service clock starts at redeem. Example: 3 days = 4320 minutes.
          </p>
        </div>
        <div className="space-y-1">
          <Label className="text-xs">Redeem Before</Label>
          <Input
            type="date"
            value={expires}
            onChange={(e) => setExpires(e.target.value)}
          />
        </div>
        <div className="space-y-1">
          <Label className="text-xs">Speed Limit</Label>
          <Select
            value={
              speedMode === SPEED_CUSTOM
                ? SPEED_CUSTOM
                : speedMode === SPEED_DEFAULT
                  ? SPEED_DEFAULT
                  : profileName
            }
            onValueChange={(value) => {
              if (value === SPEED_CUSTOM) {
                setSpeedMode(SPEED_CUSTOM);
                return;
              }
              if (value === SPEED_DEFAULT) {
                setSpeedMode(SPEED_DEFAULT);
                setProfileName(ROUTER_DEFAULT_PROFILE);
                return;
              }
              setSpeedMode(value);
              setProfileName(value);
            }}
          >
            <SelectTrigger>
              <SelectValue
                placeholder={profilesLoading ? "Loading…" : "Select speed"}
              />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value={SPEED_DEFAULT}>
                Use Promo/Profile Default
              </SelectItem>
              <SelectItem value={SPEED_CUSTOM}>Custom</SelectItem>
              {profileDetails.map((p) => (
                <SelectItem key={p.name} value={p.name}>
                  {p.name} ({formatRateLimit(p.rateLimit)})
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          {speedMode === SPEED_CUSTOM ? (
            <div className="grid grid-cols-2 gap-2 pt-1">
              <div>
                <Label className="text-[10px]">Download Mbps</Label>
                <NumericInput
                  value={customDown}
                  min={1}
                  max={1000}
                  onValueChange={setCustomDown}
                />
              </div>
              <div>
                <Label className="text-[10px]">Upload Mbps</Label>
                <NumericInput
                  value={customUp}
                  min={1}
                  max={1000}
                  onValueChange={setCustomUp}
                />
              </div>
            </div>
          ) : selectedDetail && speedMode !== SPEED_DEFAULT ? (
            <p className="text-[11px] text-muted-foreground">
              RouterOS rate-limit: {formatRateLimit(selectedDetail.rateLimit)}
            </p>
          ) : (
            <p className="text-[11px] text-muted-foreground">
              Uses the HotSpot profile configured in router settings / promo.
            </p>
          )}
        </div>
        <div className="space-y-1 col-span-2">
          <Label className="text-xs">Display Speed (optional)</Label>
          <Input
            value={displaySpeed}
            onChange={(e) => setDisplaySpeed(e.target.value)}
            placeholder="e.g. 5 Mbps"
          />
          <p className="text-[11px] text-muted-foreground">
            Presentation only (printed label / UI). Does not change RouterOS
            enforcement — use Speed Limit above for actual rate-limit.
          </p>
        </div>
      </div>
      {generating ? (
        <p className="text-sm text-muted-foreground pt-1">
          Generating {count ?? VOUCHER_COUNT_DEFAULT} voucher
          {(count ?? VOUCHER_COUNT_DEFAULT) === 1 ? "" : "s"}…
          <br />
          Please wait.
        </p>
      ) : null}
      <DialogFooter>
        <Button
          disabled={generating}
          onClick={() => {
            const c = count;
            const a = amount;
            const m = minutes;
            if (
              c === undefined ||
              !Number.isInteger(c) ||
              c < VOUCHER_COUNT_MIN ||
              c > VOUCHER_COUNT_MAX
            ) {
              toast.error(
                `Count must be ${VOUCHER_COUNT_MIN}–${VOUCHER_COUNT_MAX}`,
              );
              return;
            }
            if (a === undefined || !Number.isFinite(a) || a < 0) {
              toast.error("Amount is required (₱0 or more)");
              return;
            }
            if (m === undefined || !Number.isInteger(m) || m < 1) {
              toast.error("Validity minutes are required (1 or more)");
              return;
            }
            const payload: VoucherGeneratePayload = {
              count: c,
              amount: a,
              minutes: m,
              expires: expires || undefined,
              speed: displaySpeed || undefined,
            };
            if (speedMode === SPEED_CUSTOM) {
              const down = customDown ?? 0;
              const up = customUp ?? 0;
              if (down < 1 || up < 1) {
                toast.error("Custom download and upload Mbps are required");
                return;
              }
              payload.downloadMbps = down;
              payload.uploadMbps = up;
              payload.profileName = `renzfi-speed-${down}m-${up}m`;
              if (!payload.speed) payload.speed = `${down}/${up} Mbps`;
            } else if (
              speedMode !== SPEED_DEFAULT &&
              profileName !== ROUTER_DEFAULT_PROFILE
            ) {
              payload.profileName = profileName;
            }
            if (import.meta.env.DEV) {
              console.debug("[voucher] generate payload", payload);
            }
            onGenerate(payload);
          }}
        >
          {generating
            ? `Generating ${count ?? VOUCHER_COUNT_DEFAULT}…`
            : "Generate"}
        </Button>
      </DialogFooter>
    </DialogContent>
  );
}
