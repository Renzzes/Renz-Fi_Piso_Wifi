import { useState } from "react";
import { useMutation } from "@tanstack/react-query";
import { Download, Trash2 } from "lucide-react";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { useLiveLogs } from "@/hooks/api/useLiveLogs";
import { logsApi } from "@/services/logs";

function levelClass(lvl: string) {
  if (lvl === "ERROR" || lvl === "ERR") return "text-red-400";
  if (lvl === "WARN") return "text-amber-400";
  if (lvl === "OK") return "text-emerald-400";
  return "text-sky-400";
}

export default function LogsPage() {
  const [q, setQ] = useState("");
  const { logs, totalCount, connected, containerRef, onScroll, clear } = useLiveLogs(q);

  const clearMutation = useMutation({
    mutationFn: () => clear(),
  });

  return (
    <div>
      <PageHeader
        title="System Logs"
        description="Live serial-monitor view of firmware events (500-entry RAM buffer)"
        actions={
          <div className="flex gap-2">
            <Button
              size="sm"
              variant="outline"
              className="border-zinc-700 bg-zinc-900 text-zinc-100 hover:bg-zinc-800"
              onClick={() => window.open(logsApi.exportUrl(), "_blank")}
            >
              <Download className="h-3.5 w-3.5" /> Export
            </Button>
            <Button
              size="sm"
              variant="outline"
              className="border-zinc-700 bg-zinc-900 text-zinc-100 hover:bg-zinc-800"
              onClick={() => clearMutation.mutate()}
            >
              <Trash2 className="h-3.5 w-3.5" /> Clear
            </Button>
          </div>
        }
      />

      <div className="rounded-md border border-zinc-800 bg-black overflow-hidden">
        <div className="flex items-center gap-3 px-3 py-2 border-b border-zinc-800 bg-zinc-950">
          <Input
            className="h-8 max-w-sm bg-black border-zinc-700 text-[#d4d4d4] font-mono text-xs placeholder:text-zinc-500"
            placeholder="Filter logs (client-side)"
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />
          <span
            className={
              "text-[11px] font-mono " + (connected ? "text-emerald-400" : "text-amber-400")
            }
          >
            {connected ? "● LIVE" : "○ reconnecting…"}
          </span>
          <span className="text-[11px] text-zinc-500 font-mono">
            {totalCount}/{500} buffered
          </span>
        </div>

        <div
          ref={containerRef}
          onScroll={onScroll}
          className="bg-black text-[#d4d4d4] p-3 font-mono text-[11px] leading-relaxed space-y-0 min-h-[520px] max-h-[520px] overflow-auto w-full"
          style={{ fontFamily: "Consolas, Monaco, 'Courier New', monospace" }}
        >
          {logs.map((l, i) => (
            <div key={`${l.id ?? "x"}-${i}`} className="flex gap-2 px-1 py-0.5 hover:bg-zinc-900/80">
              <span className="text-zinc-500 shrink-0 w-[108px] truncate">{l.t}</span>
              <span className={"shrink-0 w-12 " + levelClass(l.lvl)}>{l.lvl}</span>
              <span className="shrink-0 w-20 text-violet-400 truncate">{l.type ?? "system"}</span>
              <span className="whitespace-pre-wrap break-all">{l.msg}</span>
            </div>
          ))}
          {logs.length === 0 && (
            <div className="text-center text-zinc-500 py-8">No matching log entries</div>
          )}
        </div>
      </div>
    </div>
  );
}
