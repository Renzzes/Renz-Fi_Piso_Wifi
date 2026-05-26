import { useState } from "react";
import { createFileRoute } from "@tanstack/react-router";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { StatCard } from "@/components/StatCard";
import { Coins } from "lucide-react";

export const Route = createFileRoute("/_layout/coin-settings")({
  component: CoinPage,
});

function CoinPage() {
  const [pulse, setPulse] = useState(100);
  const [calibration, setCalibration] = useState(1);
  const [timeout_, setTimeout_] = useState(30);

  return (
    <div>
      <PageHeader title="Coin Settings" description="Configure the coin slot acceptor" />

      <div className="grid grid-cols-2 md:grid-cols-4 gap-2 mb-3">
        <StatCard label="Last Pulse" value="0" icon={Coins} />
        <StatCard label="Total Today" value="248" icon={Coins} tone="success" />
        <StatCard label="Errors" value="0" tone="success" />
        <StatCard label="State" value="Ready" tone="success" />
      </div>

      <div className="grid md:grid-cols-2 gap-3">
        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Configuration</div>
          <div className="space-y-1">
            <Label className="text-xs">Pulse Width (ms)</Label>
            <Input type="number" value={pulse} onChange={(e) => setPulse(+e.target.value)} />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Coin Calibration (pulses per peso)</Label>
            <Input type="number" value={calibration} onChange={(e) => setCalibration(+e.target.value)} />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Insert Timeout (seconds)</Label>
            <Input type="number" value={timeout_} onChange={(e) => setTimeout_(+e.target.value)} />
          </div>
          <Button size="sm">Save</Button>
        </div>

        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Diagnostics</div>
          <div className="rounded-sm bg-muted/50 p-2 font-mono text-xs h-40 overflow-auto">
            <div>[12:04:11] Pulse detected (1)</div>
            <div>[12:04:14] Pulse detected (5)</div>
            <div>[12:04:18] Coin accepted: ₱5</div>
            <div>[12:09:02] Timeout</div>
            <div>[12:11:34] Pulse detected (10)</div>
          </div>
          <div className="flex gap-2">
            <Button size="sm" variant="outline">Test Pulse</Button>
            <Button size="sm" variant="outline">Reset</Button>
          </div>
        </div>
      </div>
    </div>
  );
}
