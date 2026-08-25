import { useState } from "react";
import { useMutation } from "@tanstack/react-query";
import { Download, Trash2 } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
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
import { useLiveLogs } from "@/hooks/api/useLiveLogs";
import { logsApi } from "@/services/logs";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import { cn } from "@/lib/utils";

function levelClass(lvl: string) {
  if (lvl === "ERROR" || lvl === "ERR") return "text-red-600 dark:text-red-400";
  if (lvl === "WARN") return "text-amber-600 dark:text-amber-400";
  if (lvl === "OK") return "text-emerald-600 dark:text-emerald-400";
  return "text-sky-600 dark:text-sky-400";
}

export default function LogsPage() {
  const [q, setQ] = useState("");
  const [clearOpen, setClearOpen] = useState(false);
  const { logs, totalCount, connected, containerRef, onScroll, clear } = useLiveLogs(q);

  const clearMutation = useMutation({
    mutationFn: () => clear(),
    onSuccess: () => setClearOpen(false),
  });

  return (
    <div className="flex w-full max-w-none flex-col gap-3">
      <div className="flex flex-col gap-3 lg:flex-row lg:items-start lg:justify-between">
        <div>
          <h2 className="text-2xl font-semibold leading-tight">System Logs</h2>
          <p className="mt-0.5 text-[13px] text-muted-foreground">
            Live serial-monitor view of firmware events (500-entry RAM buffer)
          </p>
        </div>
        <div className="flex flex-nowrap items-center gap-2.5 overflow-x-auto">
          <Button
            size="sm"
            variant="outline"
            className="h-9 shrink-0 px-4"
            onClick={() => window.open(logsApi.exportUrl(), "_blank")}
          >
            <Download className="h-4 w-4" /> Export
          </Button>
          <Button
            size="sm"
            variant="outline"
            className="h-9 shrink-0 px-4 border-red-500/40 bg-red-500/10 text-red-600 hover:bg-red-500/20 dark:text-red-400"
            disabled={clearMutation.isPending}
            onClick={() => setClearOpen(true)}
          >
            <Trash2 className="h-4 w-4" /> Clear
          </Button>
        </div>
      </div>

      <div className="overflow-hidden rounded-[14px] border bg-card">
        <div className="flex flex-wrap items-center gap-3 border-b px-3 py-2">
          <div className="min-w-0 flex-1">
            <Input
              className="h-9 w-full min-w-0 font-mono text-xs"
              placeholder="Filter logs (client-side)"
              value={q}
              onChange={(e) => setQ(e.target.value)}
              aria-label="Filter logs"
            />
          </div>
          <ConfigStatusBadge
            label={connected ? "Live" : "Reconnecting"}
            tone={connected ? "ok" : "warn"}
          />
          <span className="font-mono text-[11px] text-muted-foreground">
            {totalCount}/500 buffered
          </span>
        </div>

        <div
          ref={containerRef}
          onScroll={onScroll}
          className="max-h-[min(70vh,640px)] min-h-[240px] w-full overflow-auto bg-muted/20 p-3 font-mono text-[11px] leading-relaxed"
        >
          {logs.map((l, i) => (
            <div
              key={`${l.id ?? "x"}-${i}`}
              className="flex flex-col gap-0.5 rounded-sm px-1 py-1 hover:bg-muted/60 sm:flex-row sm:gap-2 sm:py-0.5"
            >
              <div className="flex min-w-0 gap-2">
                <span className="w-[108px] shrink-0 truncate text-muted-foreground">{l.t}</span>
                <span className={cn("w-12 shrink-0", levelClass(l.lvl))}>{l.lvl}</span>
                <span className="w-20 shrink-0 truncate text-violet-600 dark:text-violet-400">
                  {l.type ?? "system"}
                </span>
              </div>
              <span className="min-w-0 whitespace-pre-wrap break-all">{l.msg}</span>
            </div>
          ))}
          {logs.length === 0 ? (
            <div className="py-8 text-center text-muted-foreground">No matching log entries</div>
          ) : null}
        </div>
      </div>

      <AlertDialog open={clearOpen} onOpenChange={setClearOpen}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Clear system logs?</AlertDialogTitle>
            <AlertDialogDescription>
              This clears the in-memory log buffer on the appliance. New events will continue to
              appear after clear.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel disabled={clearMutation.isPending}>Cancel</AlertDialogCancel>
            <AlertDialogAction
              className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
              disabled={clearMutation.isPending}
              onClick={(event) => {
                event.preventDefault();
                clearMutation.mutate();
              }}
            >
              {clearMutation.isPending ? "Clearing…" : "Clear"}
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}
