import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";

export const Route = createFileRoute("/_layout/logs")({
  component: LogsPage,
});

const seed = [
  { t: "12:32:01", lvl: "INFO", msg: "Voucher T4N9-LZBV activated for 10.10.10.34" },
  { t: "12:30:55", lvl: "OK", msg: "Coin accepted: ₱5 from slot" },
  { t: "12:30:22", lvl: "WARN", msg: "Hotspot user session timeout 10.10.10.58" },
  { t: "12:28:14", lvl: "INFO", msg: "MikroTik connection healthy (32ms)" },
  { t: "12:25:00", lvl: "INFO", msg: "ESP32 boot complete - 3d 4h uptime" },
  { t: "12:20:31", lvl: "ERR", msg: "Failed login attempt for user 'admin'" },
];

function LogsPage() {
  const [q, setQ] = useState("");
  const filtered = seed.filter((l) => l.msg.toLowerCase().includes(q.toLowerCase()));
  return (
    <div>
      <PageHeader
        title="Logs"
        description="System events and diagnostics"
        actions={<Button size="sm" variant="outline">Clear</Button>}
      />
      <Input className="h-8 mb-3 max-w-sm" placeholder="Search logs" value={q}
        onChange={(e) => setQ(e.target.value)} />
      <div className="rounded-md border bg-card p-2 font-mono text-xs space-y-0.5 max-h-[480px] overflow-auto">
        {filtered.map((l, i) => (
          <div key={i} className="flex gap-2 px-1 py-0.5 hover:bg-muted/50 rounded-sm">
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
        {filtered.length === 0 && (
          <div className="text-center text-muted-foreground py-6">No matching logs</div>
        )}
      </div>
    </div>
  );
}
