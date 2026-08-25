import { useEffect, useMemo, useState } from "react";
import { Pause, Play, WifiOff, Wifi, Ban } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";
import {
  useActiveUsers,
  useDisconnectUser,
  usePauseUser,
  useReconnectUser,
  useResumeUser,
  useTerminateUser,
} from "@/hooks/api/useActiveUsers";
import { useQuery } from "@tanstack/react-query";
import { salesApi } from "@/services/sales";
import type { ActiveUser, SessionState } from "@/types/api";
import { cn } from "@/lib/utils";
import { toast } from "sonner";
import { AdminTableCard } from "@/components/admin/AdminTableCard";
import { DataPagination } from "@/components/admin/DataPagination";
import { EmptyState } from "@/components/admin/EmptyState";
import { Skeleton } from "@/components/ui/skeleton";
import { clampPage, PAGE_SIZE_DEFAULT, pageSlice } from "@/lib/pagination";

type ConfirmAction = "pause" | "resume" | "disconnect" | "reconnect" | "terminate";

function formatRemainingMinutes(minutes: number) {
  if (minutes <= 0) return "—";
  if (minutes >= 60) {
    const hours = Math.floor(minutes / 60);
    const mins = minutes % 60;
    return mins > 0 ? `${hours}h ${mins}m` : `${hours}h`;
  }
  return `${minutes}m`;
}

function sessionTypeBadge(user: ActiveUser) {
  const coin = user.sessionType === "coin";
  return (
    <span
      className={cn(
        "inline-flex items-center rounded-full border px-2 py-0.5 text-[11px] font-medium",
        coin
          ? "border-blue-500/25 bg-blue-500/15 text-blue-700 dark:text-blue-300"
          : "border-violet-500/25 bg-violet-500/15 text-violet-700 dark:text-violet-300",
      )}
    >
      {coin ? "Coin" : "Voucher"}
    </span>
  );
}

function SessionStateBadge({ state }: { state: SessionState }) {
  const styles: Record<string, string> = {
    paused: "border-amber-500/25 bg-amber-500/15 text-amber-800 dark:text-amber-300",
    waiting_coin: "border-sky-500/25 bg-sky-500/15 text-sky-700 dark:text-sky-300",
    activating: "border-sky-500/25 bg-sky-500/15 text-sky-700 dark:text-sky-300",
    activation_error: "border-red-500/25 bg-red-500/15 text-red-700 dark:text-red-400",
    expiring: "border-orange-500/25 bg-orange-500/15 text-orange-800 dark:text-orange-300",
    active: "border-emerald-500/25 bg-emerald-500/15 text-emerald-700 dark:text-emerald-300",
    expired: "border-red-500/25 bg-red-500/15 text-red-700 dark:text-red-400",
    idle: "border-border bg-muted text-muted-foreground",
  };
  const labels: Record<string, string> = {
    paused: "Paused",
    waiting_coin: "Waiting Coin",
    activating: "Activating",
    activation_error: "Activation Error",
    expiring: "Expiring",
    active: "Active",
    expired: "Expired",
    idle: "Idle",
  };
  return (
    <span
      className={cn(
        "inline-flex items-center rounded-full border px-2 py-0.5 text-[11px] font-medium",
        styles[state] ?? "border-border bg-muted text-muted-foreground",
      )}
    >
      {labels[state] ?? state ?? "—"}
    </span>
  );
}

function UserActions({
  user,
  onConfirm,
  busy,
}: {
  user: ActiveUser;
  onConfirm: (action: ConfirmAction, mac: string) => void;
  busy: boolean;
}) {
  const isVoucher = user.sessionType === "voucher" || user.source === "voucher";
  const canPause = !isVoucher && user.state === "active";
  const canResume = !isVoucher && user.state === "paused";
  const canReconnect =
    user.state === "paused" ||
    user.state === "activation_error" ||
    (isVoucher && user.state === "active" && user.remainingMinutes > 0);
  const controlsDisabled = user.state === "waiting_coin" || busy;

  return (
    <div className="flex flex-nowrap justify-end gap-1">
      {!isVoucher && (
        <>
          <Button
            size="sm"
            variant="outline"
            className="h-7 px-2 text-[11px]"
            disabled={!canPause || controlsDisabled}
            onClick={() => onConfirm("pause", user.mac)}
          >
            <Pause className="h-3.5 w-3.5" /> Pause
          </Button>
          <Button
            size="sm"
            variant="outline"
            className="h-7 px-2 text-[11px]"
            disabled={!canResume || controlsDisabled}
            onClick={() => onConfirm("resume", user.mac)}
          >
            <Play className="h-3.5 w-3.5" /> Resume
          </Button>
        </>
      )}
      {canReconnect ? (
        <Button
          size="sm"
          variant="outline"
          className="h-7 px-2 text-[11px]"
          disabled={busy}
          onClick={() => onConfirm("reconnect", user.mac)}
        >
          <Wifi className="h-3.5 w-3.5" /> Connect
        </Button>
      ) : null}
      <Button
        size="sm"
        variant="outline"
        className="h-7 px-2 text-[11px]"
        disabled={busy}
        onClick={() => onConfirm("disconnect", user.mac)}
      >
        <WifiOff className="h-3.5 w-3.5" /> Disconnect
      </Button>
      <Button
        size="sm"
        variant="outline"
        className="h-7 px-2 text-[11px] border-red-500/40 bg-red-500/10 text-red-600 hover:bg-red-500/20 dark:text-red-400"
        disabled={busy}
        onClick={() => onConfirm("terminate", user.mac)}
      >
        <Ban className="h-3.5 w-3.5" /> Terminate
      </Button>
    </div>
  );
}

export default function ActiveUsersPage() {
  const { data: users, isLoading, isError, refetch } = useActiveUsers();
  const pauseUser = usePauseUser();
  const resumeUser = useResumeUser();
  const disconnect = useDisconnectUser();
  const reconnect = useReconnectUser();
  const terminate = useTerminateUser();
  const [confirm, setConfirm] = useState<{ action: ConfirmAction; mac: string } | null>(null);
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(PAGE_SIZE_DEFAULT);

  const list = users ?? [];
  const count = list.length;
  const pausedCount = list.filter((u) => u.state === "paused").length;
  const busy =
    pauseUser.isPending ||
    resumeUser.isPending ||
    disconnect.isPending ||
    reconnect.isPending ||
    terminate.isPending;

  const safePage = clampPage(page, count, pageSize);
  const pageRows = pageSlice(list, safePage, pageSize);

  useEffect(() => {
    if (page !== safePage) setPage(safePage);
  }, [page, safePage]);

  useEffect(() => {
    setPage(1);
  }, [pageSize]);

  const runConfirmedAction = async () => {
    if (!confirm) return;
    const { action, mac } = confirm;
    setConfirm(null);
    try {
      if (action === "pause") {
        await pauseUser.mutateAsync(mac);
        toast.success("Session paused");
      } else if (action === "resume") {
        await resumeUser.mutateAsync(mac);
        toast.success("Session resumed");
      } else if (action === "reconnect") {
        await reconnect.mutateAsync(mac);
        toast.success("Reconnect queued — remaining time preserved");
      } else if (action === "terminate") {
        await terminate.mutateAsync(mac);
        toast.success("Session terminated by owner");
      } else {
        await disconnect.mutateAsync(mac);
        toast.success("Internet disconnected — time paused");
      }
    } catch (err) {
      toast.error(err instanceof Error ? err.message : "Action failed");
    }
  };

  const dialogCopy =
    confirm?.action === "pause"
      ? {
          title: "Pause Session?",
          description:
            "The customer session will be paused. Remaining time is preserved until you resume.",
          action: "Pause session",
        }
      : confirm?.action === "resume"
        ? {
            title: "Resume Session?",
            description: "The customer session will resume and the countdown will continue.",
            action: "Resume session",
          }
        : confirm?.action === "reconnect"
          ? {
              title: "Reconnect Customer?",
              description:
                "Restore internet access without adding time. The countdown continues from the remaining balance.",
              action: "Connect",
            }
          : confirm?.action === "terminate"
            ? {
                title: "Terminate Session?",
                description:
                  "This ends the session, resets remaining time to zero, removes internet access, and shows an owner notice on the captive portal.",
                action: "Terminate session",
              }
            : {
                title: "Disconnect Internet?",
                description:
                  "Removes internet access and pauses the countdown. Use Connect to restore without adding time.",
                action: "Disconnect",
              };

  return (
    <div className="space-y-3">
      <div className="flex flex-col gap-1 sm:flex-row sm:items-end sm:justify-between">
        <div>
          <h2 className="text-2xl font-semibold leading-tight">Active Users</h2>
          <p className="mt-0.5 text-[13px] text-muted-foreground">
            Monitor currently connected WiFi users and sessions
          </p>
        </div>
        <p className="text-[13px] text-muted-foreground">
          {isLoading
            ? "Loading active sessions..."
            : `${count} active session${count === 1 ? "" : "s"}${
                pausedCount > 0 ? ` · ${pausedCount} paused` : ""
              }`}
        </p>
      </div>

      {isError ? (
        <div className="rounded-[14px] border bg-card p-4">
          <p className="text-sm font-medium">Unable to load active users</p>
          <p className="mt-1 text-[13px] text-muted-foreground">
            Something went wrong while retrieving session data.
          </p>
          <Button type="button" size="sm" className="mt-3" onClick={() => void refetch()}>
            Retry
          </Button>
        </div>
      ) : (
        <AdminTableCard
          footer={
            !isLoading && count > 0 ? (
              <DataPagination
                page={safePage}
                pageSize={pageSize}
                total={count}
                onPageChange={setPage}
                onPageSizeChange={setPageSize}
                itemLabel="sessions"
              />
            ) : null
          }
        >
          <Table>
            <TableHeader>
              <TableRow className="hover:bg-transparent">
                <TableHead className="h-10 bg-muted/40 text-[12px]">MAC</TableHead>
                <TableHead className="h-10 bg-muted/40 text-[12px]">IP</TableHead>
                <TableHead className="h-10 bg-muted/40 text-[12px]">Session Type</TableHead>
                <TableHead className="h-10 bg-muted/40 text-[12px]">Session State</TableHead>
                <TableHead className="h-10 bg-muted/40 text-[12px]">Remaining Time</TableHead>
                <TableHead className="h-10 bg-muted/40 text-[12px]">Credits</TableHead>
                <TableHead className="h-10 bg-muted/40 text-[12px]">Portal</TableHead>
                <TableHead className="h-10 bg-muted/40 text-right text-[12px]">Actions</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {isLoading
                ? Array.from({ length: 6 }).map((_, i) => (
                    <TableRow key={`sk-${i}`} className="h-12">
                      {Array.from({ length: 8 }).map((__, cell) => (
                        <TableCell key={cell}>
                          <Skeleton className="h-3.5 w-full max-w-[7rem]" />
                        </TableCell>
                      ))}
                    </TableRow>
                  ))
                : pageRows.map((u) => (
                    <TableRow
                      key={u.mac}
                      className={cn(
                        "h-12",
                        u.state === "paused" &&
                          "bg-amber-500/10 hover:bg-amber-500/15 border-l-2 border-l-amber-500",
                      )}
                    >
                      <TableCell className="font-mono text-[12px]">{u.mac || "—"}</TableCell>
                      <TableCell className="font-mono text-[12px]">{u.ip || "—"}</TableCell>
                      <TableCell>{sessionTypeBadge(u)}</TableCell>
                      <TableCell>
                        <SessionStateBadge state={u.state} />
                      </TableCell>
                      <TableCell className="tabular-nums text-[13px]">
                        {formatRemainingMinutes(u.remainingMinutes)}
                      </TableCell>
                      <TableCell className="tabular-nums text-[13px]">{u.credits}</TableCell>
                      <TableCell className="text-[12px] text-muted-foreground">
                        {u.portalHeartbeatFresh === true
                          ? "Open"
                          : u.portalHeartbeatFresh === false
                            ? "Closed"
                            : "—"}
                      </TableCell>
                      <TableCell className="text-right">
                        <UserActions
                          user={u}
                          onConfirm={(action, mac) => setConfirm({ action, mac })}
                          busy={busy}
                        />
                      </TableCell>
                    </TableRow>
                  ))}
            </TableBody>
          </Table>
          {!isLoading && count === 0 ? (
            <EmptyState
              title="No Active Users"
              description="There are currently no connected WiFi sessions."
            />
          ) : null}
        </AdminTableCard>
      )}

      <div className="space-y-2">
        <div>
          <h3 className="text-sm font-semibold">User History</h3>
          <p className="text-[12px] text-muted-foreground">
            Profile is the MikroTik HotSpot user profile applied when the customer paid. Speed is
            the promo bandwidth availed (for example 10/10 Mbps). Coin sessions record speed from
            the matched promo at Done Paying. Recent completed sessions from sales persistence (same
            ledger as Sales Reports).
          </p>
        </div>
        <UserHistoryTable />
      </div>

      <AlertDialog open={confirm !== null} onOpenChange={(open) => !open && setConfirm(null)}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>{dialogCopy.title}</AlertDialogTitle>
            <AlertDialogDescription>{dialogCopy.description}</AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>Cancel</AlertDialogCancel>
            <AlertDialogAction onClick={() => void runConfirmedAction()}>
              {dialogCopy.action}
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}

function UserHistoryTable() {
  type Period = "day" | "weekly" | "monthly" | "range";
  const [period, setPeriod] = useState<Period>("day");
  const [rangeFrom, setRangeFrom] = useState("");
  const [rangeTo, setRangeTo] = useState("");
  const [page, setPage] = useState(1);
  const [pageSize, setPageSize] = useState(PAGE_SIZE_DEFAULT);

  const { data: records = [], isLoading } = useQuery({
    queryKey: ["sales", "records", "history-table"],
    queryFn: () => salesApi.records(200),
  });

  const filtered = useMemo(() => {
    const now = new Date();
    const startOfDay = new Date(now.getFullYear(), now.getMonth(), now.getDate());
    let fromMs = startOfDay.getTime();
    let toMs = now.getTime() + 24 * 60 * 60 * 1000;

    if (period === "weekly") {
      fromMs = startOfDay.getTime() - 6 * 24 * 60 * 60 * 1000;
    } else if (period === "monthly") {
      fromMs = startOfDay.getTime() - 29 * 24 * 60 * 60 * 1000;
    } else if (period === "range") {
      if (rangeFrom) fromMs = new Date(`${rangeFrom}T00:00:00`).getTime();
      if (rangeTo) toMs = new Date(`${rangeTo}T23:59:59`).getTime();
    }

    return records.filter((r) => {
      const stamp = r.connectedAt || r.recordedAt || r.recorded_at || "";
      const t = Date.parse(stamp);
      if (!Number.isFinite(t)) return period === "day";
      return t >= fromMs && t <= toMs;
    });
  }, [records, period, rangeFrom, rangeTo]);

  useEffect(() => {
    setPage(1);
  }, [period, rangeFrom, rangeTo, pageSize]);

  const safePage = clampPage(page, filtered.length, pageSize);
  const pageRows = pageSlice(filtered, safePage, pageSize);

  return (
    <div className="space-y-2">
      <div className="flex flex-wrap items-end gap-2">
        <div className="space-y-1">
          <Label className="text-xs">History period</Label>
          <Select value={period} onValueChange={(v) => setPeriod(v as Period)}>
            <SelectTrigger className="h-8 w-[160px]">
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="day">Day</SelectItem>
              <SelectItem value="weekly">Weekly</SelectItem>
              <SelectItem value="monthly">Monthly</SelectItem>
              <SelectItem value="range">Date range</SelectItem>
            </SelectContent>
          </Select>
        </div>
        {period === "range" ? (
          <>
            <div className="space-y-1">
              <Label className="text-xs">From</Label>
              <Input
                type="date"
                className="h-8"
                value={rangeFrom}
                onChange={(e) => setRangeFrom(e.target.value)}
              />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">To</Label>
              <Input
                type="date"
                className="h-8"
                value={rangeTo}
                onChange={(e) => setRangeTo(e.target.value)}
              />
            </div>
          </>
        ) : null}
      </div>
      <AdminTableCard
        footer={
          !isLoading && filtered.length > 0 ? (
            <DataPagination
              page={safePage}
              pageSize={pageSize}
              total={filtered.length}
              onPageChange={setPage}
              onPageSizeChange={setPageSize}
              itemLabel="sessions"
            />
          ) : null
        }
      >
        <Table>
          <TableHeader>
            <TableRow className="hover:bg-transparent">
              <TableHead className="h-10 bg-muted/40 text-[12px]">Username / Voucher</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">MAC</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Start</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">End</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Duration</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Amount</TableHead>
              <TableHead
                className="h-10 bg-muted/40 text-[12px]"
                title="MikroTik HotSpot user profile at payment"
              >
                Profile
              </TableHead>
              <TableHead
                className="h-10 bg-muted/40 text-[12px]"
                title="Promo bandwidth availed (Mbps)"
              >
                Speed
              </TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Status</TableHead>
              <TableHead className="h-10 bg-muted/40 text-[12px]">Source</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {isLoading
              ? Array.from({ length: 4 }).map((_, i) => (
                  <TableRow key={`hsk-${i}`} className="h-12">
                    {Array.from({ length: 10 }).map((__, cell) => (
                      <TableCell key={cell}>
                        <Skeleton className="h-3.5 w-full max-w-[6rem]" />
                      </TableCell>
                    ))}
                  </TableRow>
                ))
              : pageRows.map((r) => (
                  <TableRow key={r.id || r.sessionId} className="h-12">
                    <TableCell className="font-mono text-[12px]">
                      {r.voucherCode || r.sessionId || "—"}
                    </TableCell>
                    <TableCell className="font-mono text-[12px]">{r.macAddress || "—"}</TableCell>
                    <TableCell className="text-[12px]">
                      {r.connectedAt || r.recordedAt || r.recorded_at || "—"}
                    </TableCell>
                    <TableCell className="text-[12px]">{r.endedAt || r.expiresAt || "—"}</TableCell>
                    <TableCell className="tabular-nums text-[12px]">
                      {r.durationMinutes != null ? `${r.durationMinutes}m` : "—"}
                    </TableCell>
                    <TableCell className="tabular-nums text-[12px]">₱{r.amount ?? 0}</TableCell>
                    <TableCell className="text-[12px]">{r.profile || "—"}</TableCell>
                    <TableCell className="text-[12px]">{r.speed || "—"}</TableCell>
                    <TableCell className="text-[12px]">{r.status || "—"}</TableCell>
                    <TableCell className="text-[12px]">{r.paymentType || "—"}</TableCell>
                  </TableRow>
                ))}
          </TableBody>
        </Table>
        {!isLoading && filtered.length === 0 ? (
          <EmptyState title="No session history for this period" />
        ) : null}
      </AdminTableCard>
    </div>
  );
}
