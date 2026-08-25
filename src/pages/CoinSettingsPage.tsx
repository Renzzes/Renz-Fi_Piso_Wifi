import { useState, useEffect } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
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
import { coinApi } from "@/services/coin";
import { toast } from "sonner";
import { useRealtime } from "@/contexts/RealtimeContext";
import { ConfigCard, ConfigField } from "@/components/system-config/ConfigCard";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import { MetricTile } from "@/components/system-config/InfoRow";
import type { StatusTone } from "@/lib/dashboardDisplay";

function coinStateTone(state: string | undefined): StatusTone {
  const value = (state ?? "").toLowerCase();
  if (!value || value === "—") return "unknown";
  if (value.includes("error") || value.includes("fault")) return "bad";
  if (value.includes("wait") || value.includes("idle")) return "ok";
  return "neutral";
}

export default function CoinSettingsPage() {
  const qc = useQueryClient();
  const { fallbackPollMs } = useRealtime();
  const { data: settings, isLoading: settingsLoading } = useQuery({
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
  const stateLabel = String(stats?.state ?? "—");

  return (
    <div className="flex w-full max-w-none flex-col gap-4">
      <div>
        <h2 className="text-2xl font-semibold leading-tight">Coin Settings</h2>
        <p className="mt-0.5 text-[13px] text-muted-foreground">Configure the coin slot acceptor</p>
      </div>

      <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-4">
        <MetricTile label="Last Pulse" value={String(stats?.last_pulse ?? "0")} />
        <MetricTile label="Total Today" value={String(stats?.total_today ?? "0")} />
        <MetricTile label="Errors" value={String(stats?.errors ?? "0")} />
        <div className="min-w-0 rounded-lg border bg-muted/20 px-3 py-2.5">
          <div className="text-[11px] font-medium text-muted-foreground">State</div>
          <div className="mt-1">
            <ConfigStatusBadge label={stateLabel} tone={coinStateTone(stateLabel)} />
          </div>
        </div>
      </div>

      <div className="grid grid-cols-1 items-start gap-4 lg:grid-cols-2">
        <ConfigCard title="Configuration">
          <ConfigField label="Pulse Width (ms)">
            <Input
              type="number"
              value={pulse}
              onChange={(e) => setPulse(+e.target.value)}
              className="min-w-0 w-full"
              disabled={settingsLoading}
            />
          </ConfigField>
          <ConfigField label="Coin Calibration (pulses per peso)">
            <Input
              type="number"
              value={calibration}
              onChange={(e) => setCalibration(+e.target.value)}
              className="min-w-0 w-full"
              disabled={settingsLoading}
            />
          </ConfigField>
          <ConfigField label="Insert Timeout (seconds)">
            <Input
              type="number"
              value={timeout_}
              onChange={(e) => setTimeout_(+e.target.value)}
              className="min-w-0 w-full"
              disabled={settingsLoading}
            />
          </ConfigField>
          <Button
            size="sm"
            disabled={saveMutation.isPending || settingsLoading}
            onClick={() => saveMutation.mutate()}
          >
            {saveMutation.isPending ? "Saving…" : "Save"}
          </Button>
        </ConfigCard>

        <ConfigCard title="Diagnostics">
          <div className="max-h-[240px] min-h-[120px] overflow-auto rounded-lg border bg-muted/20 p-3 font-mono text-xs">
            {(diagnostics?.logs ?? []).length === 0 ? (
              <p className="text-muted-foreground">No diagnostic messages</p>
            ) : (
              (diagnostics?.logs ?? []).map((l, i) => (
                <div key={i}>
                  [{l.t}] {l.msg}
                </div>
              ))
            )}
          </div>
          <div className="flex flex-col gap-2 sm:flex-row">
            <Button
              size="sm"
              variant="outline"
              disabled={testMutation.isPending}
              onClick={() => testMutation.mutate()}
            >
              {testMutation.isPending ? "Testing…" : "Test Pulse"}
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
        </ConfigCard>
      </div>
    </div>
  );
}
