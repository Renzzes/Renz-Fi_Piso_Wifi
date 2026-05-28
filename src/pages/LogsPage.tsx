import { useState } from "react";
import { useMutation, useQueryClient } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { useLogs } from "@/hooks/api/useLogs";
import { logsApi } from "@/services/logs";

export default function LogsPage() {
  const [q, setQ] = useState("");
  const { data: logs = [] } = useLogs(q);
  const qc = useQueryClient();

  const clearMutation = useMutation({
    mutationFn: () => logsApi.clear(),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["logs"] }),
  });

  return (
    <div>
      <PageHeader
        title="Logs"
        description="System events and diagnostics"
        actions={
          <div className="flex gap-2">
            <Button
              size="sm"
              variant="outline"
              onClick={() => window.open(logsApi.exportUrl(), "_blank")}
            >
              Export
            </Button>
            <Button size="sm" variant="outline" onClick={() => clearMutation.mutate()}>
              Clear
            </Button>
          </div>
        }
      />
      <Input
        className="h-8 mb-3 max-w-sm"
        placeholder="Search logs"
        value={q}
        onChange={(e) => setQ(e.target.value)}
      />
      <div className="rounded-md border bg-card p-2 font-mono text-xs space-y-0.5 max-h-[480px] overflow-auto">
        {logs.map((l, i) => (
          <div key={l.id ?? i} className="flex gap-2 px-1 py-0.5 hover:bg-muted/50 rounded-sm">
            <span className="text-muted-foreground shrink-0">{l.t}</span>
            <span
              className={
                "shrink-0 w-12 " +
                (l.lvl === "ERR"
                  ? "text-red-500"
                  : l.lvl === "WARN"
                    ? "text-amber-500"
                    : l.lvl === "OK"
                      ? "text-emerald-500"
                      : "text-blue-500")
              }
            >
              {l.lvl}
            </span>
            <span className="truncate">{l.msg}</span>
          </div>
        ))}
        {logs.length === 0 && (
          <div className="text-center text-muted-foreground py-6">No matching logs</div>
        )}
      </div>
    </div>
  );
}
