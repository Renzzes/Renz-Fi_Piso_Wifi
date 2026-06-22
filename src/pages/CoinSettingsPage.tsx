import { useState, useEffect } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { StatCard } from "@/components/StatCard";
import { Coins } from "lucide-react";
import { coinApi } from "@/services/coin";
import { toast } from "sonner";
import { useRealtime } from "@/contexts/RealtimeContext";

export default function CoinSettingsPage() {
  const qc = useQueryClient();
  const { fallbackPollMs } = useRealtime();
  const { data: settings } = useQuery({
    queryKey: ["coin", "settings"],
    queryFn: () => coinApi.settings(),
  });
  const { data: diagnostics } = useQuery({
    queryKey: ["coin", "diagnostics"],
    queryFn: () => coinApi.diagnostics(),
    refetchInterval: fallbackPollMs,
    staleTime: 5000,
    refetchIntervalInBackground: false,
  });

  const [pulse, setPulse] = useState(100);
  const [calibration, setCalibration] = useState(1);
  const [timeout_, setTimeout_] = useState(30);

  useEffect(() => {
    if (settings) {
      setPulse(Number(settings.pulse_width_ms ?? 100));
      setCalibration(Number(settings.calibration ?? 1));
      setTimeout_(Number(settings.timeout_seconds ?? 30));
    }
  }, [settings]);

  const saveMutation = useMutation({
    mutationFn: () =>
      coinApi.save({
        pulse_width_ms: String(pulse),
        calibration: String(calibration),
        timeout_seconds: String(timeout_),
      }),
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["coin"] });
      toast.success("Coin settings saved");
    },
  });

  const testMutation = useMutation({
    mutationFn: () => coinApi.test(),
    onSuccess: () => toast.success("Test pulse acknowledged"),
  });

  const resetMutation = useMutation({
    mutationFn: () => coinApi.reset(),
    onSuccess: () => {
      qc.invalidateQueries({ queryKey: ["coin", "diagnostics"] });
      toast.success("Coin diagnostics reset");
    },
    onError: () => toast.error("Failed to reset coin diagnostics"),
  });

  const stats = diagnostics?.stats;

  return (
    <div>
      <PageHeader title="Coin Settings" description="Configure the coin slot acceptor" />

      <div className="grid grid-cols-2 md:grid-cols-4 gap-2 mb-3">
        <StatCard label="Last Pulse" value={stats?.last_pulse ?? "0"} icon={Coins} />
        <StatCard
          label="Total Today"
          value={stats?.total_today ?? "0"}
          icon={Coins}
          tone="success"
        />
        <StatCard label="Errors" value={stats?.errors ?? "0"} tone="success" />
        <StatCard label="State" value={stats?.state ?? "—"} tone="success" />
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
            <Input
              type="number"
              value={calibration}
              onChange={(e) => setCalibration(+e.target.value)}
            />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Insert Timeout (seconds)</Label>
            <Input type="number" value={timeout_} onChange={(e) => setTimeout_(+e.target.value)} />
          </div>
          <Button size="sm" onClick={() => saveMutation.mutate()}>
            Save
          </Button>
        </div>

        <div className="rounded-md border bg-card p-3 space-y-3">
          <div className="text-sm font-medium">Diagnostics</div>
          <div className="rounded-sm bg-muted/50 p-2 font-mono text-xs h-40 overflow-auto">
            {(diagnostics?.logs ?? []).map((l, i) => (
              <div key={i}>
                [{l.t}] {l.msg}
              </div>
            ))}
          </div>
          <div className="flex gap-2">
            <Button size="sm" variant="outline" onClick={() => testMutation.mutate()}>
              Test Pulse
            </Button>
            <AlertDialog>
              <AlertDialogTrigger asChild>
                <Button size="sm" variant="outline" disabled={resetMutation.isPending}>
                  Reset
                </Button>
              </AlertDialogTrigger>
              <AlertDialogContent>
                <AlertDialogHeader>
                  <AlertDialogTitle>Reset coin diagnostics?</AlertDialogTitle>
                  <AlertDialogDescription>
                    This clears today&apos;s pulse counters, error count, and the diagnostics log.
                    Portal credits and active sessions are not affected.
                  </AlertDialogDescription>
                </AlertDialogHeader>
                <AlertDialogFooter>
                  <AlertDialogCancel>Cancel</AlertDialogCancel>
                  <AlertDialogAction onClick={() => resetMutation.mutate()}>
                    Reset counters
                  </AlertDialogAction>
                </AlertDialogFooter>
              </AlertDialogContent>
            </AlertDialog>
          </div>
        </div>
      </div>
    </div>
  );
}
