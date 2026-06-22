import { useState } from "react";
import { Pause, Play, WifiOff } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
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
  if (state === "active") {
    return (
      <Badge className="border-transparent bg-emerald-600 text-white hover:bg-emerald-600/90">
        Active
      </Badge>
    );
  }
  return <Badge variant="outline">—</Badge>;
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
  const canPause = user.state === "active";
  const canResume = user.state === "paused";
  const controlsDisabled = user.state === "waiting_coin" || busy;

  return (
    <div className="flex justify-end gap-1">
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
  const [confirm, setConfirm] = useState<{ action: ConfirmAction; mac: string } | null>(
    null,
  );

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
              <TableHead className="text-right">Actions</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {isLoading ? (
              <TableRow>
                <TableCell colSpan={7} className="text-center text-muted-foreground py-6 text-sm">
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
                  <TableCell className="text-right">
                    <UserActions user={u} onConfirm={(action, mac) => setConfirm({ action, mac })} busy={busy} />
                  </TableCell>
                </TableRow>
              ))
            )}
            {!isLoading && (users ?? []).length === 0 && (
              <TableRow>
                <TableCell colSpan={7} className="text-center text-muted-foreground py-6 text-sm">
                  No Active Users
                </TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
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
