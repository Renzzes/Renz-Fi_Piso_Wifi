import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from "react";
import {
  buildDeviceBaseUrl,
  isDirectMode,
  isFleetTargetActive,
  setRuntimeApiBaseUrl,
} from "@/services/embeddedApi";
import {
  clearCurrentDeviceId,
  getRegisteredDevice,
  isFleetModeEnabled,
  readCurrentDeviceId,
  readDeviceRegistry,
  readDiscoverySubnet,
  removeRegisteredDevice,
  setFleetModeEnabled,
  upsertRegisteredDevice,
  writeCurrentDeviceId,
  writeDiscoverySubnet,
  writeDeviceRegistry,
} from "@/services/deviceRegistry";
import { discoverDevicesOnSubnet } from "@/services/deviceDiscovery";
import {
  fetchDeviceProfileFromCurrentTarget,
  refreshAllDeviceHealth,
} from "@/services/deviceHealth";
import {
  readFleetPollIntervalMs,
  writeFleetPollIntervalMs,
} from "@/services/fleetHealthPreferences";
import { buildFleetApplianceHealth } from "@/services/fleetHealthService";
import type { RegisteredDevice } from "@/types/deviceProfile";
import type { FleetApplianceHealth, FleetPollIntervalMs } from "@/types/fleetHealth";

export type DeviceRegistryContextValue = {
  devices: RegisteredDevice[];
  currentDevice: RegisteredDevice | null;
  fleetMode: boolean;
  directMode: boolean;
  discovering: boolean;
  discoveryProgress: { scanned: number; total: number } | null;
  discoverySubnet: string;
  fleetHealthByDevice: Record<string, FleetApplianceHealth>;
  pollIntervalMs: FleetPollIntervalMs;
  setPollIntervalMs: (ms: FleetPollIntervalMs) => void;
  setDiscoverySubnet: (subnet: string) => void;
  refreshDevices: () => Promise<void>;
  refreshFleetHealth: () => Promise<void>;
  discoverDevices: () => Promise<RegisteredDevice[]>;
  switchDevice: (deviceId: string) => void;
  addOrUpdateDevice: (device: RegisteredDevice) => void;
  removeDevice: (deviceId: string) => void;
  enableFleetMode: () => void;
  registerCurrentAppliance: () => Promise<RegisteredDevice | null>;
};

const DeviceRegistryContext = createContext<DeviceRegistryContextValue | null>(null);

export function DeviceRegistryProvider({
  children,
  onDeviceSwitch,
}: {
  children: ReactNode;
  onDeviceSwitch?: (device: RegisteredDevice) => void;
}) {
  const [devices, setDevices] = useState<RegisteredDevice[]>(() => readDeviceRegistry());
  const [currentDeviceId, setCurrentDeviceId] = useState<string | null>(() =>
    readCurrentDeviceId(),
  );
  const [fleetMode, setFleetMode] = useState(
    () => isFleetModeEnabled() || readDeviceRegistry().length > 1,
  );
  const [discovering, setDiscovering] = useState(false);
  const [discoveryProgress, setDiscoveryProgress] = useState<{
    scanned: number;
    total: number;
  } | null>(null);
  const [discoverySubnet, setDiscoverySubnetState] = useState(readDiscoverySubnet);
  const [fleetHealthByDevice, setFleetHealthByDevice] = useState<
    Record<string, FleetApplianceHealth>
  >({});
  const [pollIntervalMs, setPollIntervalMsState] = useState<FleetPollIntervalMs>(() =>
    readFleetPollIntervalMs(),
  );

  const currentDevice = useMemo(
    () => devices.find((d) => d.deviceId === currentDeviceId) ?? null,
    [devices, currentDeviceId],
  );

  const directMode = isDirectMode() && !isFleetTargetActive();

  const syncFromStorage = useCallback(() => {
    setDevices(readDeviceRegistry());
    setCurrentDeviceId(readCurrentDeviceId());
  }, []);

  const applyDeviceTarget = useCallback(
    (device: RegisteredDevice) => {
      if (directMode && !fleetMode) {
        setRuntimeApiBaseUrl(null);
      } else {
        setRuntimeApiBaseUrl(buildDeviceBaseUrl(device.ip));
      }
      writeCurrentDeviceId(device.deviceId);
      setCurrentDeviceId(device.deviceId);
      onDeviceSwitch?.(device);
    },
    [directMode, fleetMode, onDeviceSwitch],
  );

  const switchDevice = useCallback(
    (deviceId: string) => {
      const device = getRegisteredDevice(deviceId);
      if (!device) return;
      applyDeviceTarget(device);
    },
    [applyDeviceTarget],
  );

  const addOrUpdateDevice = useCallback(
    (device: RegisteredDevice) => {
      upsertRegisteredDevice(device);
      syncFromStorage();
    },
    [syncFromStorage],
  );

  const removeDevice = useCallback(
    (deviceId: string) => {
      removeRegisteredDevice(deviceId);
      syncFromStorage();
      if (currentDeviceId === deviceId) {
        clearCurrentDeviceId();
        setCurrentDeviceId(null);
        setRuntimeApiBaseUrl(null);
      }
    },
    [currentDeviceId, syncFromStorage],
  );

  const refreshFleetHealth = useCallback(async () => {
    const list = readDeviceRegistry();
    if (list.length === 0) {
      setFleetHealthByDevice({});
      return;
    }
    const probes = await refreshAllDeviceHealth(list);
    const next: Record<string, FleetApplianceHealth> = {};
    for (const probe of probes) {
      next[probe.device.deviceId] = buildFleetApplianceHealth(probe.device, probe.snapshot);
    }
    writeDeviceRegistry(probes.map((p) => p.device));
    setFleetHealthByDevice(next);
    syncFromStorage();
  }, [syncFromStorage]);

  const refreshDevices = useCallback(async () => {
    await refreshFleetHealth();
  }, [refreshFleetHealth]);

  const discoverDevices = useCallback(async () => {
    setDiscovering(true);
    setDiscoveryProgress({ scanned: 0, total: 254 });
    try {
      const found = await discoverDevicesOnSubnet({
        subnet: discoverySubnet,
        onProgress: (p) => setDiscoveryProgress({ scanned: p.scanned, total: p.total }),
      });
      syncFromStorage();
      return found;
    } finally {
      setDiscovering(false);
      setDiscoveryProgress(null);
    }
  }, [discoverySubnet, syncFromStorage]);

  const registerCurrentAppliance = useCallback(async () => {
    const device = await fetchDeviceProfileFromCurrentTarget();
    if (device) {
      upsertRegisteredDevice(device);
      if (!readCurrentDeviceId()) {
        applyDeviceTarget(device);
      }
      syncFromStorage();
    }
    return device;
  }, [applyDeviceTarget, syncFromStorage]);

  const enableFleetMode = useCallback(() => {
    setFleetModeEnabled(true);
    setFleetMode(true);
  }, []);

  const setDiscoverySubnet = useCallback((subnet: string) => {
    writeDiscoverySubnet(subnet);
    setDiscoverySubnetState(subnet);
  }, []);

  const setPollIntervalMs = useCallback((ms: FleetPollIntervalMs) => {
    writeFleetPollIntervalMs(ms);
    setPollIntervalMsState(ms);
  }, []);

  useEffect(() => {
    if (currentDeviceId) {
      const device = getRegisteredDevice(currentDeviceId);
      if (device && (fleetMode || isFleetTargetActive())) {
        setRuntimeApiBaseUrl(buildDeviceBaseUrl(device.ip));
      }
    }
  }, [currentDeviceId, fleetMode]);

  useEffect(() => {
    void registerCurrentAppliance();
  }, [registerCurrentAppliance]);

  const value = useMemo<DeviceRegistryContextValue>(
    () => ({
      devices,
      currentDevice,
      fleetMode: fleetMode || devices.length > 1,
      directMode,
      discovering,
      discoveryProgress,
      discoverySubnet,
      fleetHealthByDevice,
      pollIntervalMs,
      setPollIntervalMs,
      setDiscoverySubnet,
      refreshDevices,
      refreshFleetHealth,
      discoverDevices,
      switchDevice,
      addOrUpdateDevice,
      removeDevice,
      enableFleetMode,
      registerCurrentAppliance,
    }),
    [
      devices,
      currentDevice,
      fleetMode,
      directMode,
      discovering,
      discoveryProgress,
      discoverySubnet,
      fleetHealthByDevice,
      pollIntervalMs,
      setPollIntervalMs,
      setDiscoverySubnet,
      refreshDevices,
      refreshFleetHealth,
      discoverDevices,
      switchDevice,
      addOrUpdateDevice,
      removeDevice,
      enableFleetMode,
      registerCurrentAppliance,
    ],
  );

  return (
    <DeviceRegistryContext.Provider value={value}>{children}</DeviceRegistryContext.Provider>
  );
}

export function useDeviceRegistry() {
  const ctx = useContext(DeviceRegistryContext);
  if (!ctx) {
    throw new Error("useDeviceRegistry must be used within DeviceRegistryProvider");
  }
  return ctx;
}
