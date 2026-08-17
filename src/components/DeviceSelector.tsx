import { Check, ChevronDown, Circle } from "lucide-react";
import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { useDeviceRegistry } from "@/contexts/DeviceRegistryContext";
import { cn } from "@/lib/utils";
import { Link } from "react-router-dom";

export function DeviceSelector() {
  const { devices, currentDevice, fleetMode, switchDevice } = useDeviceRegistry();

  if (!fleetMode && devices.length <= 1) {
    return currentDevice ? (
      <span className="text-xs text-muted-foreground truncate max-w-[140px]" title={currentDevice.name}>
        {currentDevice.name}
      </span>
    ) : null;
  }

  return (
    <DropdownMenu>
      <DropdownMenuTrigger asChild>
        <Button variant="outline" size="sm" className="gap-1.5 max-w-[200px]">
          <span
            className={cn(
              "h-2 w-2 rounded-full shrink-0",
              currentDevice?.isOnline ? "bg-emerald-500" : "bg-muted-foreground",
            )}
          />
          <span className="truncate">{currentDevice?.name ?? "Select device"}</span>
          <ChevronDown className="h-3.5 w-3.5 shrink-0 opacity-60" />
        </Button>
      </DropdownMenuTrigger>
      <DropdownMenuContent align="end" className="w-56">
        <DropdownMenuLabel>Devices</DropdownMenuLabel>
        <DropdownMenuSeparator />
        {devices.map((device) => (
          <DropdownMenuItem
            key={device.deviceId}
            onClick={() => switchDevice(device.deviceId)}
            className="gap-2"
          >
            {device.isOnline ? (
              <Circle className="h-2 w-2 fill-emerald-500 text-emerald-500" />
            ) : (
              <Circle className="h-2 w-2 fill-muted-foreground text-muted-foreground" />
            )}
            <span className="flex-1 truncate">{device.name}</span>
            {currentDevice?.deviceId === device.deviceId ? (
              <Check className="h-3.5 w-3.5" />
            ) : null}
          </DropdownMenuItem>
        ))}
        <DropdownMenuSeparator />
        <DropdownMenuItem asChild>
          <Link to="/devices">Manage devices…</Link>
        </DropdownMenuItem>
      </DropdownMenuContent>
    </DropdownMenu>
  );
}
