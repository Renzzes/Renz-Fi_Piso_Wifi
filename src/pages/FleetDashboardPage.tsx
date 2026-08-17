import { useCallback, useEffect, useMemo, useState } from "react";
import { Link, useNavigate } from "react-router-dom";
import { PageHeader } from "@/components/PageHeader";
import { FleetApplianceCard } from "@/components/fleet/FleetApplianceCard";
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
import { useDeviceRegistry } from "@/contexts/DeviceRegistryContext";
import { useFleetActiveDeviceEvents } from "@/hooks/useFleetActiveDeviceEvents";
import { cn } from "@/lib/utils";
import {
  FLEET_POLL_INTERVALS_MS,
  fleetHealthEmoji,
  type FleetPollIntervalMs,
} from "@/types/fleetHealth";
import { Radar, RefreshCw } from "lucide-react";
import { toast } from "sonner";

export default function FleetDashboardPage() {
  const navigate = useNavigate();
  const {
    devices,
    currentDevice,
    fleetMode,
    directMode,
    discovering,
    discoveryProgress,
    discoverySubnet,
    setDiscoverySubnet,
    discoverDevices,
    refreshFleetHealth,
    fleetHealthByDevice,
    pollIntervalMs,
    setPollIntervalMs,
    switchDevice,
    enableFleetMode,
  } = useDeviceRegistry();

  const [refreshing, setRefreshing] = useState(false);

  const fleetEntries = useMemo(
    () =>
      devices.map((device) => fleetHealthByDevice[device.deviceId] ?? {
        deviceId: device.deviceId,
        device,
        level: device.isOnline ? ("warning" as const) : ("offline" as const),
        score: device.isOnline ? 50 : 0,
        warnings: device.isOnline ? ["Health pending refresh"] : ["Appliance unreachable"],
        snapshot: null,
        lastRefreshed: null,
      }),
    [devices, fleetHealthByDevice],
  );

  const summary = useMemo(() => {
    const healthy = fleetEntries.filter((e) => e.level === "healthy").length;
    const warning = fleetEntries.filter((e) => e.level === "warning").length;
    const offline = fleetEntries.filter((e) => e.level === "offline").length;
    return { healthy, warning, offline, total: fleetEntries.length };
  }, [fleetEntries]);

  const handleRefresh = useCallback(async () => {
    setRefreshing(true);
    try {
      await refreshFleetHealth();
      toast.message("Fleet health updated.");
    } catch {
      toast.error("Fleet refresh failed.");
    } finally {
      setRefreshing(false);
    }
  }, [refreshFleetHealth]);

  const handleActiveDeviceEvent = useCallback(() => {
    void refreshFleetHealth();
  }, [refreshFleetHealth]);

  useFleetActiveDeviceEvents(
    Boolean(currentDevice?.deviceId),
    currentDevice?.deviceId ?? null,
    handleActiveDeviceEvent,
  );

  useEffect(() => {
    if (devices.length === 0) return;
    void refreshFleetHealth();
    const timer = window.setInterval(() => {
      void refreshFleetHealth();
    }, pollIntervalMs);
    return () => window.clearInterval(timer);
  }, [devices.length, pollIntervalMs, refreshFleetHealth]);

  const handleDiscover = async () => {
    try {
      enableFleetMode();
      const found = await discoverDevices();
      await refreshFleetHealth();
      toast.success(
        found.length > 0
          ? `Discovered ${found.length} appliance${found.length === 1 ? "" : "s"}.`
          : "No appliances found on this subnet.",
      );
    } catch {
      toast.error("Discovery failed.");
    }
  };

  const handleSelectDevice = (deviceId: string) => {
    switchDevice(deviceId);
    toast.message("Active appliance switched.");
    navigate("/dashboard");
  };

  return (
    <div className="space-y-6">
      <PageHeader
        title="Fleet Health"
        description="Monitor all registered Renz-Fi appliances. Health is computed in this browser — each appliance remains fully autonomous."
      />

      <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
        <div className="rounded-lg border bg-card p-3 text-center">
          <p className="text-2xl font-semibold">{summary.total}</p>
          <p className="text-xs text-muted-foreground">Registered</p>
        </div>
        <div className="rounded-lg border bg-card p-3 text-center">
          <p className="text-2xl font-semibold text-emerald-600">
            {fleetHealthEmoji("healthy")} {summary.healthy}
          </p>
          <p className="text-xs text-muted-foreground">Healthy</p>
        </div>
        <div className="rounded-lg border bg-card p-3 text-center">
          <p className="text-2xl font-semibold text-amber-600">
            {fleetHealthEmoji("warning")} {summary.warning}
          </p>
          <p className="text-xs text-muted-foreground">Warning</p>
        </div>
        <div className="rounded-lg border bg-card p-3 text-center">
          <p className="text-2xl font-semibold text-red-600">
            {fleetHealthEmoji("offline")} {summary.offline}
          </p>
          <p className="text-xs text-muted-foreground">Offline</p>
        </div>
      </div>

      <div className="rounded-lg border bg-card p-4 space-y-4">
        <div className="flex flex-col lg:flex-row lg:items-end gap-3">
          <div className="flex-1 space-y-2">
            <Label htmlFor="discovery-subnet">Discovery subnet</Label>
            <Input
              id="discovery-subnet"
              value={discoverySubnet}
              onChange={(e) => setDiscoverySubnet(e.target.value)}
              placeholder="192.168.88"
            />
          </div>
          <div className="space-y-2">
            <Label htmlFor="poll-interval">Auto refresh</Label>
            <Select
              value={String(pollIntervalMs)}
              onValueChange={(v) => setPollIntervalMs(Number(v) as FleetPollIntervalMs)}
            >
              <SelectTrigger id="poll-interval" className="w-[140px]">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {FLEET_POLL_INTERVALS_MS.map((ms) => (
                  <SelectItem key={ms} value={String(ms)}>
                    {ms / 1000}s
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>
          <div className="flex gap-2">
            <Button type="button" variant="outline" disabled={discovering} onClick={() => void handleDiscover()}>
              <Radar className="h-4 w-4 mr-2" />
              {discovering ? "Discovering…" : "Discover"}
            </Button>
            <Button
              type="button"
              variant="outline"
              disabled={refreshing || devices.length === 0}
              onClick={() => void handleRefresh()}
            >
              <RefreshCw className={cn("h-4 w-4 mr-2", refreshing && "animate-spin")} />
              Refresh
            </Button>
          </div>
        </div>

        {discovering && discoveryProgress ? (
          <p className="text-xs text-muted-foreground">
            Scanning… {discoveryProgress.scanned} / {discoveryProgress.total}
          </p>
        ) : null}

        <p className="text-xs text-muted-foreground">
          Mode: {directMode && !fleetMode ? "Direct (single appliance)" : "Fleet (multi-device)"} ·
          Polling every {pollIntervalMs / 1000}s · SSE live updates for active appliance only
        </p>
      </div>

      <div className="space-y-3">
        {fleetEntries.length === 0 ? (
          <div className="rounded-lg border border-dashed p-8 text-center text-sm text-muted-foreground">
            No appliances registered. Run discovery or open an appliance directly to auto-register it.
          </div>
        ) : (
          fleetEntries.map((entry) => (
            <FleetApplianceCard
              key={entry.deviceId}
              health={entry}
              isActive={currentDevice?.deviceId === entry.deviceId}
              onSelect={() => handleSelectDevice(entry.deviceId)}
            />
          ))
        )}
      </div>

      <p className="text-xs text-muted-foreground">
        <Link to="/dashboard" className="text-primary hover:underline">
          Back to dashboard
        </Link>
        {" · "}
        Click an appliance card to switch the active target. No shared data between appliances.
      </p>
    </div>
  );
}
