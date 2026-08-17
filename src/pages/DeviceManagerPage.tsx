import { useState } from "react";
import { Link } from "react-router-dom";
import { PageHeader } from "@/components/PageHeader";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { useDeviceRegistry } from "@/contexts/DeviceRegistryContext";
import { cn } from "@/lib/utils";
import { Circle, RefreshCw, Radar, Trash2 } from "lucide-react";
import { toast } from "sonner";

export default function DeviceManagerPage() {
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
    refreshDevices,
    switchDevice,
    removeDevice,
    enableFleetMode,
  } = useDeviceRegistry();

  const [refreshing, setRefreshing] = useState(false);

  const handleDiscover = async () => {
    try {
      enableFleetMode();
      const found = await discoverDevices();
      toast.success(
        found.length > 0
          ? `Discovered ${found.length} appliance${found.length === 1 ? "" : "s"}.`
          : "No appliances found on this subnet.",
      );
    } catch {
      toast.error("Discovery failed.");
    }
  };

  const handleRefresh = async () => {
    setRefreshing(true);
    try {
      await refreshDevices();
      toast.message("Device status updated.");
    } finally {
      setRefreshing(false);
    }
  };

  return (
    <div className="space-y-6">
      <PageHeader
        title="Devices"
        description="Manage registered appliances. For health monitoring and fleet overview, use Fleet Health."
      />
      <p className="text-sm">
        <Link to="/fleet" className="text-primary hover:underline">
          Open Fleet Health dashboard
        </Link>
      </p>

      <div className="rounded-lg border bg-card p-4 space-y-4">
        <div className="flex flex-col sm:flex-row sm:items-end gap-3">
          <div className="flex-1 space-y-2">
            <Label htmlFor="discovery-subnet">Discovery subnet</Label>
            <Input
              id="discovery-subnet"
              value={discoverySubnet}
              onChange={(e) => setDiscoverySubnet(e.target.value)}
              placeholder="192.168.88"
            />
            <p className="text-xs text-muted-foreground">
              Scans {discoverySubnet}.1–254 via GET /api/health
            </p>
          </div>
          <div className="flex gap-2">
            <Button
              type="button"
              variant="outline"
              disabled={discovering}
              onClick={() => void handleDiscover()}
            >
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
          Mode: {directMode && !fleetMode ? "Direct (single appliance)" : "Fleet (multi-device)"}
        </p>
      </div>

      <div className="space-y-2">
        {devices.length === 0 ? (
          <div className="rounded-lg border border-dashed p-8 text-center text-sm text-muted-foreground">
            No devices registered. Run discovery or open an appliance directly to auto-register it.
          </div>
        ) : (
          devices.map((device) => (
            <div
              key={device.deviceId}
              className={cn(
                "flex flex-col sm:flex-row sm:items-center gap-3 rounded-lg border p-4",
                currentDevice?.deviceId === device.deviceId && "border-primary/40 bg-primary/5",
              )}
            >
              <div className="flex items-start gap-3 flex-1 min-w-0">
                <Circle
                  className={cn(
                    "h-3 w-3 mt-1 shrink-0",
                    device.isOnline
                      ? "fill-emerald-500 text-emerald-500"
                      : "fill-muted-foreground text-muted-foreground",
                  )}
                />
                <div className="min-w-0">
                  <p className="font-medium truncate">{device.name}</p>
                  <p className="text-xs text-muted-foreground font-mono truncate">
                    {device.deviceId} · {device.ip}
                  </p>
                  <p className="text-xs text-muted-foreground truncate">
                    {device.firmwareVersion}
                    {device.routerDriver ? ` · ${device.routerDriver}` : ""}
                  </p>
                </div>
              </div>
              <div className="flex gap-2 shrink-0">
                <Button
                  type="button"
                  size="sm"
                  variant={currentDevice?.deviceId === device.deviceId ? "secondary" : "default"}
                  disabled={!device.isOnline}
                  onClick={() => switchDevice(device.deviceId)}
                >
                  {currentDevice?.deviceId === device.deviceId ? "Active" : "Switch"}
                </Button>
                <Button
                  type="button"
                  size="sm"
                  variant="ghost"
                  onClick={() => removeDevice(device.deviceId)}
                  aria-label={`Remove ${device.name}`}
                >
                  <Trash2 className="h-4 w-4" />
                </Button>
              </div>
            </div>
          ))
        )}
      </div>

      <p className="text-xs text-muted-foreground">
        <Link to="/dashboard" className="text-primary hover:underline">
          Back to dashboard
        </Link>
        {" · "}
        Switching devices preserves your current page and retargets API calls only.
      </p>
    </div>
  );
}
