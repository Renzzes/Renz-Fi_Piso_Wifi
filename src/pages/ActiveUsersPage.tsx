import { useMemo, useState } from "react";
import { Pause, Play, WifiOff } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
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
import { Badge } from "@/components/ui/badge";
import {
  useActiveUsers,
  useDisconnectUser,
  usePauseUser,
  useResumeUser,
} from "@/hooks/api/useActiveUsers";
import { useQuery } from "@tanstack/react-query";
import { salesApi } from "@/services/sales";
import type { ActiveUser, SessionState } from "@/types/api";
import { cn } from "@/lib/utils";
import { toast } from "sonner";

type ConfirmAction = "pause" | "resume" | "disconnect";

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
  return (
    <Badge variant={user.sessionType === "coin" ? "default" : "secondary"}>
      {user.sessionType === "coin" ? "Coin" : "Voucher"}
    </Badge>
  );
}

function SessionStateBadge({ state }: { state: SessionState }) {
  if (state === "paused") {
    return (
      <Badge className="border-amber-500/50 bg-amber-500/15 text-amber-700 dark:text-amber-300 hover:bg-amber-500/20">
        Paused
      </Badge>
    );
  }
  if (state === "waiting_coin") {
    return (
      <Badge className="border-sky-500/50 bg-sky-500/15 text-sky-700 dark:text-sky-300 hover:bg-sky-500/20">
        Waiting Coin
      </Badge>
    );
  }
  if (state === "activating") {
    return (
      <Badge className="border-sky-500/50 bg-sky-500/15 text-sky-700 dark:text-sky-300 hover:bg-sky-500/20">
        Activating
      </Badge>
    );
  }
  if (state === "activation_error") {
    return (
      <Badge className="border-red-500/50 bg-red-500/15 text-red-700 dark:text-red-300 hover:bg-red-500/20">
        Activation Error
      </Badge>
    );
  }
  if (state === "expiring") {
    return (
      <Badge className="border-orange-500/50 bg-orange-500/15 text-orange-700 dark:text-orange-300 hover:bg-orange-500/20">
        Expiring
      </Badge>
    );
  }
  if (state === "active") {
    return (
      <Badge className="border-transparent bg-emerald-600 text-white hover:bg-emerald-600/90">
        Active
      </Badge>
    );
  }
  return <Badge variant="outline">{state || "—"}</Badge>;
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
  const controlsDisabled = user.state === "waiting_coin" || busy;

  return (
    <div className="flex justify-end gap-1">
      {!isVoucher && (
        <>
          <Button
            size="sm"
            variant="outline"
            className="h-7"
            disabled={!canPause || controlsDisabled}
            onClick={() => onConfirm("pause", user.mac)}
          >
            <Pause className="h-3.5 w-3.5" /> Pause
          </Button>
          <Button
            size="sm"
            variant="outline"
            className="h-7"
            disabled={!canResume || controlsDisabled}
            onClick={() => onConfirm("resume", user.mac)}
          >
            <Play className="h-3.5 w-3.5" /> Resume
          </Button>
        </>
      )}
      <Button
        size="sm"
        variant="destructive"
        className="h-7"
        disabled={busy}
        onClick={() => onConfirm("disconnect", user.mac)}
      >
        <WifiOff className="h-3.5 w-3.5" /> Disconnect
      </Button>
    </div>
  );
}

export default function ActiveUsersPage() {
  const { data: users, isLoading } = useActiveUsers();
  const pauseUser = usePauseUser();
  const resumeUser = useResumeUser();
  const disconnect = useDisconnectUser();
  const [confirm, setConfirm] = useState<{ action: ConfirmAction; mac: string } | null>(null);

  const count = users?.length ?? 0;
  const pausedCount = (users ?? []).filter((u) => u.state === "paused").length;
  const busy = pauseUser.isPending || resumeUser.isPending || disconnect.isPending;

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
      } else {
        await disconnect.mutateAsync(mac);
        toast.success("User disconnected");
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
        : {
            title: "Disconnect User?",
            description: "This ends the session and removes the user from the active list.",
            action: "Disconnect",
          };

  return (
    <div>
      <PageHeader
        title="Active Users"
        description={
          isLoading
            ? "Loading active sessions..."
            : `${count} active session${count === 1 ? "" : "s"}${pausedCount > 0 ? ` · ${pausedCount} paused` : ""}`
        }
      />
      <div className="rounded-md border bg-card overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>MAC</TableHead>
              <TableHead>IP</TableHead>
              <TableHead>Session Type</TableHead>
              <TableHead>Session State</TableHead>
              <TableHead>Remaining Time</TableHead>
              <TableHead>Credits</TableHead>
              <TableHead>Portal</TableHead>
              <TableHead className="text-right">Actions</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {isLoading ? (
              <TableRow>
                <TableCell colSpan={8} className="text-center text-muted-foreground py-6 text-sm">
                  Loading...
                </TableCell>
              </TableRow>
            ) : (
              (users ?? []).map((u) => (
                <TableRow
                  key={u.mac}
                  className={cn(
                    u.state === "paused" &&
                      "bg-amber-500/10 hover:bg-amber-500/15 border-l-2 border-l-amber-500",
                  )}
                >
                  <TableCell className="font-mono text-xs">{u.mac || "—"}</TableCell>
                  <TableCell className="font-mono text-xs">{u.ip || "—"}</TableCell>
                  <TableCell>{sessionTypeBadge(u)}</TableCell>
                  <TableCell>
                    <SessionStateBadge state={u.state} />
                  </TableCell>
                  <TableCell className="tabular-nums">
                    {formatRemainingMinutes(u.remainingMinutes)}
                  </TableCell>
                  <TableCell className="tabular-nums">{u.credits}</TableCell>
                  <TableCell className="text-xs text-muted-foreground">
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
              ))
            )}
            {!isLoading && (users ?? []).length === 0 && (
              <TableRow>
                <TableCell colSpan={8} className="text-center text-muted-foreground py-6 text-sm">
                  No Active Users
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </div>

      <div className="mt-6">
        <h2 className="text-sm font-semibold mb-2">User History</h2>
        <p className="text-xs text-muted-foreground mb-2">
          Recent completed sessions from sales persistence (same ledger as Sales Reports).
        </p>
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
      if (!Number.isFinite(t)) return period === "day"; // keep undated in Day view
      return t >= fromMs && t <= toMs;
    });
  }, [records, period, rangeFrom, rangeTo]);

  return (
    <div className="space-y-2">
      <div className="flex flex-wrap items-end gap-2">
        <div className="space-y-1">
          <Label className="text-xs">History period</Label>
          <Select
            value={period}
            onValueChange={(v) => setPeriod(v as Period)}
          >
            <SelectTrigger className="w-[160px] h-8">
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
      <div className="rounded-md border bg-card overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Username / Voucher</TableHead>
              <TableHead>MAC</TableHead>
              <TableHead>Start</TableHead>
              <TableHead>End</TableHead>
              <TableHead>Duration</TableHead>
              <TableHead>Amount</TableHead>
              <TableHead>Profile</TableHead>
              <TableHead>Speed</TableHead>
              <TableHead>Status</TableHead>
              <TableHead>Source</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {isLoading ? (
              <TableRow>
                <TableCell colSpan={10} className="text-center text-muted-foreground py-4 text-sm">
                  Loading history…
                </TableCell>
              </TableRow>
            ) : filtered.length === 0 ? (
              <TableRow>
                <TableCell colSpan={10} className="text-center text-muted-foreground py-4 text-sm">
                  No session history for this period
                </TableCell>
              </TableRow>
            ) : (
              filtered.map((r) => (
                <TableRow key={r.id || r.sessionId}>
                  <TableCell className="font-mono text-xs">
                    {r.voucherCode || r.sessionId || "—"}
                  </TableCell>
                  <TableCell className="font-mono text-xs">{r.macAddress || "—"}</TableCell>
                  <TableCell className="text-xs">{r.connectedAt || r.recordedAt || r.recorded_at || "—"}</TableCell>
                  <TableCell className="text-xs">{r.endedAt || r.expiresAt || "—"}</TableCell>
                  <TableCell className="tabular-nums text-xs">
                    {r.durationMinutes != null ? `${r.durationMinutes}m` : "—"}
                  </TableCell>
                  <TableCell className="tabular-nums text-xs">₱{r.amount ?? 0}</TableCell>
                  <TableCell className="text-xs">{r.profile || "—"}</TableCell>
                  <TableCell className="text-xs">{r.speed || "—"}</TableCell>
                  <TableCell className="text-xs">{r.status || "—"}</TableCell>
                  <TableCell className="text-xs">{r.paymentType || "—"}</TableCell>
                </TableRow>
              ))
            )}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}
