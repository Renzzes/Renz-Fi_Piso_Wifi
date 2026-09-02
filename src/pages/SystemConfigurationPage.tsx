import { useState, useEffect, useMemo, useRef } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { ConnectionStatusList } from "@/components/ConnectionStatusBadge";
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
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Skeleton } from "@/components/ui/skeleton";
import { Plug, Save, Loader2, CheckCircle2, XCircle, MinusCircle, RefreshCw } from "lucide-react";
import {
  ConfigCard,
  ConfigField,
  ConfigFormGrid,
  ConfigSectionNav,
  ConfigStatusBadge,
  InfoRow,
  OverviewStatusCard,
  readSystemConfigSection,
  type SystemConfigSectionId,
} from "@/components/system-config";
import { routerApi, type RouterConfig, type RouterTestResult } from "@/services/router";
import { systemApi } from "@/services/system";
import {
  DEFAULT_MIKROTIK_IP,
  formatWirelessSecurityLabel,
  isOpenWirelessSecurity,
  normalizeRouterConfig,
  normalizeRouterWireless,
  routerConfigNeedsMigration,
  toRouterSavePayload,
  toRouterTestPayload,
  type RouterWirelessForm,
} from "@/lib/routerConfig";
import {
  hotspotServiceStatusDisplay,
  internetReachabilityDisplay,
  mikrotikApiStatusDisplay,
  type ConnectionTone,
} from "@/lib/systemConfigurationStatus";
import type { StatusTone } from "@/lib/dashboardDisplay";
import { SystemBuildInfo } from "@/components/SystemBuildInfo";
import { RouterCacheStaleBanner } from "@/components/RouterCacheStaleBanner";
import { WirelessConfigurationSummary } from "@/components/WirelessConfigurationSummary";
import { StorageHealthCard } from "@/components/StorageHealthCard";
import { cn } from "@/lib/utils";
import {
  formatRouterCacheAge,
  routerCacheLastSyncLabel,
  routerCacheProductionWifiLabel,
  routerCacheProvisionStatusLabel,
} from "@/lib/routerCacheStatus";
import { productionNetworkReasonLabel } from "@/lib/productionNetworkReason";
import { refreshProductionRouterViews } from "@/lib/refreshProductionRouterViews";
import { healthApi } from "@/services/rgb";
import { toast } from "sonner";

// Config queries below load once per page visit and stay cached while the
// page is open. They only refresh when the user presses Save/Test/Refresh —
// never on a background timer — to keep RouterOS API traffic minimal.
const CONFIG_QUERY_OPTIONS = {
  staleTime: Number.POSITIVE_INFINITY,
  refetchOnMount: false as const,
};

function defaultFormState(): RouterConfig {
  return normalizeRouterConfig(null);
}

function defaultWirelessState(): RouterWirelessForm {
  return normalizeRouterWireless(null);
}

function fallbackTestSteps(ok: boolean): RouterTestResult["steps"] {
  return [
    {
      id: "api_reachable",
      label: "RouterOS API reachable",
      ok,
      message: ok ? "API endpoint responded" : "Could not reach RouterOS API",
    },
    {
      id: "login",
      label: "Login successful",
      ok,
      message: ok ? "API credentials accepted" : "API login failed",
    },
    {
      id: "profile",
      label: "Hotspot profile exists",
      ok,
      message: ok ? "Profile is configured" : "Hotspot profile not found",
    },
  ];
}

function normalizeTestResult(result: RouterTestResult | undefined): RouterTestResult {
  if (!result) return { ok: false, steps: fallbackTestSteps(false), summary: "Test failed" };

  if (result.connected !== undefined) {
    const ok =
      result.ok ?? Boolean(result.connected && result.authenticated && result.profileFound);
    const steps: RouterTestResult["steps"] = [
      {
        id: "api_reachable",
        label: "RouterOS API reachable",
        ok: Boolean(result.connected),
        message: result.connected
          ? "TCP connection to port 8728 succeeded"
          : result.error || "Could not connect to RouterOS API",
      },
      {
        id: "login",
        label: "Login successful",
        ok: Boolean(result.authenticated),
        message: result.authenticated
          ? "RouterOS API login succeeded"
          : result.error || "API login failed",
      },
      {
        id: "profile",
        label: "Hotspot profile exists",
        ok: Boolean(result.profileFound),
        message: result.profileFound
          ? `Profile verified${result.identity ? ` (router: ${result.identity})` : ""}`
          : result.error || "Hotspot profile not found",
      },
    ];
    return {
      ok,
      connected: result.connected,
      authenticated: result.authenticated,
      profileFound: result.profileFound,
      identity: result.identity,
      error: result.error,
      routerOs: result.routerOs,
      profiles: result.profiles,
      profileDetails: result.profileDetails,
      truncated: result.truncated,
      steps,
      summary:
        result.summary ??
        (ok
          ? `Connected to ${result.identity || "RouterOS"}`
          : result.error || "One or more checks failed"),
    };
  }

  const steps = result.steps?.length ? result.steps : fallbackTestSteps(result.ok);
  return {
    ok: result.ok,
    routerOs: result.routerOs,
    profiles: result.profiles,
    profileDetails: result.profileDetails,
    truncated: result.truncated,
    steps,
    summary: result.summary ?? (result.ok ? "All checks passed" : "One or more checks failed"),
  };
}

function formatUptime(rawSeconds: string | undefined): string {
  if (!rawSeconds) return "—";
  // RouterOS already formats /system/resource uptime as e.g. "3d5h12m3s".
  return rawSeconds;
}

function formatMemory(freeBytes: string | undefined, totalBytes: string | undefined): string {
  const free = Number(freeBytes);
  const total = Number(totalBytes);
  if (!Number.isFinite(free) || !Number.isFinite(total) || total <= 0) return "—";
  const usedMb = (total - free) / 1024 / 1024;
  const totalMb = total / 1024 / 1024;
  return `${usedMb.toFixed(0)} / ${totalMb.toFixed(0)} MB`;
}

function connectionToneToStatus(tone: ConnectionTone): StatusTone {
  if (tone === "connected") return "ok";
  if (tone === "disconnected") return "bad";
  return "unknown";
}

function systemHealthOverview(
  level: string | undefined,
  loading: boolean,
): { label: string; tone: StatusTone } {
  if (loading) return { label: "Loading...", tone: "unknown" };
  if (level === "HEALTHY" || level === "ACTIVE_SESSION") return { label: "Healthy", tone: "ok" };
  if (level === "WARNING") return { label: "Warning", tone: "warn" };
  if (level === "ERROR") return { label: "Error", tone: "bad" };
  return { label: "Unavailable", tone: "unknown" };
}

const STANDALONE_PAGE_META: Partial<
  Record<SystemConfigSectionId, { title: string; description: string }>
> = {
  "syscfg-network": {
    title: "Network",
    description: "Configure the ESP32 Ethernet interface used by Renz-Fi.",
  },
  "syscfg-wireless": {
    title: "Wireless",
    description: "MikroTik wireless SSID and security (wireless-capable routers only).",
  },
  "syscfg-hotspot": {
    title: "Bandwidth",
    description: "HotSpot profiles, configured rate limits, and router API credentials.",
  },
  "syscfg-storage": {
    title: "Storage & Firmware",
    description: "Running firmware, build metadata, and SD card health.",
  },
  "syscfg-router": {
    title: "Router Status",
    description: "MikroTik connectivity, cache age, and synchronization controls.",
  },
};

export default function SystemConfigurationPage({
  fixedSection,
}: {
  fixedSection?: SystemConfigSectionId;
} = {}) {
  const queryClient = useQueryClient();
  const [form, setForm] = useState<RouterConfig>(defaultFormState);
  const [wirelessForm, setWirelessForm] = useState<RouterWirelessForm>(defaultWirelessState);
  const [testResult, setTestResult] = useState<RouterTestResult | null>(null);
  const [migrationApplied, setMigrationApplied] = useState(false);
  const [networkMode, setNetworkMode] = useState<"dhcp" | "static">("dhcp");
  const [staticFields, setStaticFields] = useState({
    ip: "",
    gateway: "",
    mask: "",
    dns: "",
  });
  const [editingRateLimitName, setEditingRateLimitName] = useState<string | null>(null);
  const [editingRateLimitValue, setEditingRateLimitValue] = useState("");
  const [activeSection, setActiveSection] = useState<SystemConfigSectionId>(
    () => fixedSection ?? readSystemConfigSection(),
  );
  const profileRefreshAttempted = useRef(false);

  const effectiveSection = fixedSection ?? activeSection;
  const standaloneMeta = fixedSection ? STANDALONE_PAGE_META[fixedSection] : null;

  // Section-gated loads: opening System Config must not fire every API at once
  // (proven DMA Guru when SoftAP + health/settings/wifi/cache herd).
  const sectionNetwork = effectiveSection === "syscfg-network";
  const sectionWireless = effectiveSection === "syscfg-wireless";
  const sectionHotspot = effectiveSection === "syscfg-hotspot";
  const sectionRouter = effectiveSection === "syscfg-router";
  const sectionOverview = effectiveSection === "syscfg-overview";
  const sectionStorage = effectiveSection === "syscfg-storage";
  const needSystemStatus = sectionOverview || sectionRouter || sectionStorage || sectionHotspot;

  const {
    data: rawConfig,
    isLoading: configLoading,
    isError: configError,
  } = useQuery({
    queryKey: ["router", "settings"],
    queryFn: () => routerApi.settings(),
    enabled: sectionHotspot,
    ...CONFIG_QUERY_OPTIONS,
  });

  const {
    data: routerCache,
    isFetching: cacheFetching,
    isError: cacheError,
    refetch: refetchCache,
  } = useQuery({
    queryKey: ["router", "cache"],
    queryFn: () => routerApi.cache(),
    enabled: sectionRouter || sectionOverview,
    ...CONFIG_QUERY_OPTIONS,
  });

  const {
    data: wirelessData,
    isLoading: wirelessLoading,
    isFetching: wirelessFetching,
    refetch: refetchWireless,
    error: wirelessError,
  } = useQuery({
    queryKey: ["router", "wireless"],
    queryFn: () => routerApi.wireless(),
    enabled: sectionWireless,
    ...CONFIG_QUERY_OPTIONS,
  });

  const {
    data: systemStatus,
    isLoading: statusLoading,
    isError: statusError,
    refetch: refetchStatus,
  } = useQuery({
    queryKey: ["system", "status"],
    queryFn: () => systemApi.status(),
    enabled: needSystemStatus,
    staleTime: 15_000,
    refetchInterval: false,
    refetchOnMount: true,
  });

  const {
    data: wifiConfig,
    isLoading: wifiConfigLoading,
    isError: wifiConfigError,
    refetch: refetchWifiConfig,
  } = useQuery({
    queryKey: ["system", "wifiConfig"],
    queryFn: () => systemApi.wifiConfig(),
    enabled: sectionNetwork,
    staleTime: 5_000,
    refetchOnMount: true,
  });

  const { data: systemHealth, isLoading: healthLoading } = useQuery({
    queryKey: ["system", "health"],
    queryFn: () => healthApi.get(),
    enabled: sectionOverview,
    staleTime: 15_000,
    refetchInterval: false,
  });

  const current = wifiConfig?.current;

  useEffect(() => {
    if (!wifiConfig) return;
    setNetworkMode(wifiConfig.addressMode === "static" ? "static" : "dhcp");
    setStaticFields({
      ip: wifiConfig.staticIp ?? "",
      gateway: wifiConfig.staticGateway ?? "",
      mask: wifiConfig.staticSubnetMask ?? "",
      dns: wifiConfig.staticDnsPrimary ?? "",
    });
  }, [wifiConfig]);

  useEffect(() => {
    if (!rawConfig) return;
    const normalized = normalizeRouterConfig(rawConfig);
    setForm(normalized);
    if (routerConfigNeedsMigration(rawConfig)) {
      setMigrationApplied(true);
    }
  }, [rawConfig]);

  useEffect(() => {
    if (!wirelessData) return;
    setWirelessForm(normalizeRouterWireless(wirelessData));
  }, [wirelessData]);

  const {
    data: profilesData,
    isLoading: profilesLoading,
    isFetching: profilesFetching,
    refetch: refetchProfiles,
    error: profilesError,
  } = useQuery({
    queryKey: ["router", "profiles"],
    queryFn: () => routerApi.profiles(),
    enabled: sectionHotspot,
    ...CONFIG_QUERY_OPTIONS,
  });

  const profileOptions = useMemo(() => {
    return Array.isArray(profilesData?.profiles) ? profilesData.profiles : [];
  }, [profilesData?.profiles]);

  const profileDetails = useMemo(() => {
    if (Array.isArray(profilesData?.profileDetails) && profilesData.profileDetails.length > 0) {
      return profilesData.profileDetails;
    }
    return profileOptions.map((name) => ({ name, rateLimit: "" }));
  }, [profilesData?.profileDetails, profileOptions]);

  const formatRateLimit = (rateLimit?: string) => {
    const trimmed = rateLimit?.trim() ?? "";
    return trimmed.length > 0 ? trimmed : "Not set yet";
  };

  // Prefer saved profile if still present; else MikroTik "default"; else first.
  // Do not persist auto-selection until Save.
  useEffect(() => {
    if (profileOptions.length === 0) return;
    setForm((prev) => {
      if (profileOptions.includes(prev.profile)) return prev;
      if (profileOptions.includes("default")) return { ...prev, profile: "default" };
      return { ...prev, profile: profileOptions[0] };
    });
  }, [profileOptions]);

  const selectedProfile =
    profileOptions.length === 0
      ? form.profile || undefined
      : profileOptions.includes(form.profile)
        ? form.profile
        : profileOptions.includes("default")
          ? "default"
          : profileOptions[0];

  // Cache hit: show profiles with 0 RouterOS commands.
  // Cache miss + stored credentials: ONE controlled refresh via router_worker.
  useEffect(() => {
    if (profilesLoading || configLoading || profileRefreshAttempted.current) return;
    if (profileOptions.length > 0) return;
    // Host is always present on provisioned appliances; passwordConfigured
    // means stored secret exists (blank password field = keep stored).
    const canRefresh =
      Boolean(form.host?.trim()) &&
      (form.passwordConfigured ||
        form.password.trim().length > 0 ||
        form.username.trim().length > 0);
    if (!canRefresh) return;
    profileRefreshAttempted.current = true;
    void (async () => {
      try {
        await routerApi.refreshProfiles();
        await refreshProductionRouterViews(queryClient);
      } catch {
        // No storm — leave error/empty state for the owner.
      }
    })();
  }, [
    profilesLoading,
    configLoading,
    profileOptions.length,
    form.host,
    form.passwordConfigured,
    form.password,
    form.username,
    queryClient,
  ]);

  const rateLimitMutation = useMutation({
    mutationFn: (payload: { name: string; rateLimit: string }) =>
      routerApi.profileOp({
        action: "set-rate-limit",
        name: payload.name,
        rateLimit: payload.rateLimit,
      }),
    onSuccess: async () => {
      setEditingRateLimitName(null);
      setEditingRateLimitValue("");
      await refreshProductionRouterViews(queryClient);
      toast.success("Profile rate limit updated on MikroTik");
    },
    onError: (err) =>
      toast.error(err instanceof Error ? err.message : "Failed to update rate limit"),
  });

  const refreshCacheMutation = useMutation({
    mutationFn: () => routerApi.refreshCache(),
    onSuccess: async () => {
      setTestResult(null);
      await refreshProductionRouterViews(queryClient);
      toast.success("Router information refreshed.");
    },
    onError: (err) =>
      toast.error(err instanceof Error ? err.message : "Failed to refresh router information"),
  });

  const syncRouterMutation = useMutation({
    mutationFn: () => routerApi.syncRouter(),
    onSuccess: async () => {
      // Sync overwrote router-cache — clear prior Test failure banners.
      setTestResult(null);
      await refreshProductionRouterViews(queryClient);
      toast.success("Router configuration synchronized.");
    },
    onError: (err) =>
      toast.error(
        err instanceof Error ? err.message : "Failed to synchronize router configuration",
      ),
  });

  const storageRecovering =
    Boolean(systemStatus?.storageStatus?.recoveryInProgress) ||
    Boolean(systemStatus?.storageStatus?.recoveryMode) ||
    systemStatus?.storageStatus?.mounted === false;
  // Do not gate Sync/Refresh on cacheFetching — under ETH_DMA_LOW the cache GET
  // can stay "fetching" and leave buttons disabled forever (field report).
  const routerCachePending =
    refreshCacheMutation.isPending || syncRouterMutation.isPending || storageRecovering;

  const handleRouterCacheRefresh = () => {
    refreshCacheMutation.mutate();
  };

  const saveMutation = useMutation({
    mutationFn: () => routerApi.save(toRouterSavePayload(form)),
    onSuccess: async () => {
      void queryClient.invalidateQueries({ queryKey: ["router", "settings"] });
      void queryClient.invalidateQueries({ queryKey: ["system", "status"] });
      await Promise.all([refetchProfiles(), refetchCache()]);
      setForm((prev) => ({ ...prev, password: "" }));
      setMigrationApplied(false);
      toast.success("Router and hotspot settings saved");
    },
    onError: () => toast.error("Failed to save hotspot settings"),
  });

  const saveWirelessMutation = useMutation({
    mutationFn: () =>
      routerApi.saveWireless({
        ssid: wirelessForm.ssid,
        password: wirelessForm.password.trim().length > 0 ? wirelessForm.password : undefined,
      }),
    onSuccess: async (result) => {
      if (result.error) {
        toast.error(result.error);
        return;
      }
      await Promise.all([refetchWireless(), refetchCache()]);
      void queryClient.invalidateQueries({ queryKey: ["system", "status"] });
      if (result.verified === false || result.verification === "deferred") {
        toast.success(
          "SSID change applied. Wireless clients may need to reconnect. Verification will occur on the next router synchronization.",
        );
      } else {
        toast.success("Wireless SSID updated and verified");
      }
    },
    onError: () => toast.error("Failed to update wireless settings on RouterOS"),
  });

  const saveNetworkMutation = useMutation({
    mutationFn: () =>
      systemApi.saveWifiConfig(
        networkMode === "dhcp"
          ? { addressMode: "dhcp" }
          : {
              addressMode: "static",
              staticIp: staticFields.ip,
              staticGateway: staticFields.gateway,
              staticSubnetMask: staticFields.mask,
              staticDnsPrimary: staticFields.dns,
            },
      ),
    onSuccess: (result) => {
      void queryClient.invalidateQueries({ queryKey: ["system", "wifiConfig"] });
      toast.success(
        result.rebootRequired
          ? "Network settings saved — reboot to apply"
          : "Network settings saved",
      );
    },
    onError: () => toast.error("Failed to save network settings"),
  });

  const testMutation = useMutation({
    mutationFn: () => routerApi.test(toRouterTestPayload(form)),
    onMutate: () => setTestResult(null),
    onSuccess: async (result) => {
      const normalized = normalizeTestResult(result);
      setTestResult(normalized);
      // Seed dropdown immediately from Test payload (same /ip/hotspot/user/profile list).
      if (Array.isArray(result.profiles) && result.profiles.length > 0) {
        queryClient.setQueryData(["router", "profiles"], {
          profiles: result.profiles,
          profileDetails:
            result.profileDetails ?? result.profiles.map((name) => ({ name, rateLimit: "" })),
          truncated: result.truncated === true,
          cached: false,
        });
      }
      await refreshProductionRouterViews(queryClient);
      if (normalized.ok) toast.success("Connection test passed");
      else toast.error(normalized.summary || "Connection test failed");
    },
    onError: () => {
      setTestResult({
        ok: false,
        steps: fallbackTestSteps(false),
        summary: "Connection test request failed",
      });
      toast.error("Connection test failed");
    },
  });

  const ethernetConnected = Boolean(
    systemHealth?.ethernet?.link === "UP" ||
    systemHealth?.ethernet?.driver === "UP" ||
    (typeof systemHealth?.ethernet?.ip === "string" && systemHealth.ethernet.ip.length > 0) ||
    (typeof current?.ip === "string" && current.ip.length > 0),
  );

  const ethernetIp =
    (typeof systemHealth?.ethernet?.ip === "string" && systemHealth.ethernet.ip) ||
    current?.ip ||
    "";

  const connectionItems = useMemo(
    () => [
      {
        label: "RouterOS Connectivity",
        status: testResult?.ok
          ? { label: "Online", tone: "connected" as const }
          : mikrotikApiStatusDisplay(systemStatus?.mikrotik, statusLoading),
      },
      {
        label: "Hotspot Service",
        status:
          testResult?.ok && testResult.profileFound
            ? { label: "Available", tone: "connected" as const }
            : hotspotServiceStatusDisplay(systemStatus?.hotspot, statusLoading),
      },
      {
        label: "Internet",
        status: internetReachabilityDisplay(
          systemStatus?.internet,
          statusLoading,
          systemStatus?.wan,
        ),
      },
      {
        label: "Ethernet",
        status:
          wifiConfigLoading || healthLoading
            ? { label: "Loading...", tone: "unknown" as const }
            : ethernetConnected
              ? {
                  label: ethernetIp ? `Connected (${ethernetIp})` : "Connected",
                  tone: "connected" as const,
                }
              : { label: "Disconnected", tone: "disconnected" as const },
      },
    ],
    [
      systemStatus,
      statusLoading,
      wifiConfigLoading,
      healthLoading,
      ethernetConnected,
      ethernetIp,
      testResult?.ok,
      testResult?.profileFound,
    ],
  );

  // router-cache is the single source of truth for metrics (Test may seed it).
  const routerOsSnapshot = routerCache?.routerOs ?? testResult?.routerOs;
  const securityDisplay =
    formatWirelessSecurityLabel(wirelessForm.security || routerCache?.security) || "—";
  const openWireless = isOpenWirelessSecurity(wirelessForm.security || routerCache?.security);

  const updateField = <K extends keyof RouterConfig>(key: K, value: RouterConfig[K]) => {
    setForm((prev) => ({ ...prev, [key]: value }));
  };

  const mikrotikStatus = mikrotikApiStatusDisplay(systemStatus?.mikrotik, statusLoading);
  const internetStatus = internetReachabilityDisplay(
    systemStatus?.internet,
    statusLoading,
    systemStatus?.wan,
  );
  const healthOverview = systemHealthOverview(systemHealth?.level, healthLoading);
  const tempC = systemHealth?.esp32?.chipTempC;
  const tempAvailable = Boolean(systemHealth?.esp32?.chipTempAvailable && tempC != null);
  const wirelessConfigured = wirelessData?.configured === true;
  const externalApOnly =
    wirelessData?.externalApOnly === true ||
    systemStatus?.networkProvisioning?.externalApOnly === true ||
    systemStatus?.networkProvisioning?.guestTopologyMode === "external_access_point";
  const overviewWirelessSsid = externalApOnly
    ? "Bridge-only — register AP in Networking → Access Points"
    : wirelessData?.ssid?.trim() ||
      routerCache?.productionNetwork?.ssid ||
      routerCache?.ssid ||
      "Unavailable";
  const overviewInternetIp =
    systemStatus?.wan?.ip ||
    current?.gateway ||
    systemHealth?.ethernet?.gateway ||
    current?.ip ||
    "Unavailable";
  const overviewRouterIdentity =
    routerCache?.identity || systemStatus?.mikrotik?.host || "Unavailable";
  const overviewHealthDetail =
    tempAvailable && tempC != null ? `${tempC.toFixed(1)}°C` : "Unavailable";

  const readOnlyInputClass = "w-full min-w-0 bg-muted/50 font-mono text-[13px]";
  const inputClass = "w-full min-w-0 bg-background";

  const showSection = (id: SystemConfigSectionId) => cn(effectiveSection !== id && "hidden");

  const handleSectionChange = (id: SystemConfigSectionId) => {
    if (fixedSection) return;
    setActiveSection(id);
    if (typeof window !== "undefined") {
      window.history.replaceState(null, "", `#${id}`);
    }
  };

  const showRouterCacheBanner =
    !fixedSection ||
    fixedSection === "syscfg-router" ||
    fixedSection === "syscfg-hotspot" ||
    fixedSection === "syscfg-overview";

  return (
    <div className="w-full max-w-none space-y-4">
      <div>
        <h2 className="text-2xl font-semibold leading-tight">
          {standaloneMeta?.title ?? "System Configuration"}
        </h2>
        <p className="mt-0.5 text-[13px] text-muted-foreground">
          {standaloneMeta?.description ??
            "Manage network, wireless, bandwidth, storage and router settings."}
        </p>
      </div>

      {fixedSection ? null : (
        <ConfigSectionNav active={effectiveSection} onChange={handleSectionChange} />
      )}

      {migrationApplied ? (
        <Alert>
          <AlertTitle>Configuration migrated</AlertTitle>
          <AlertDescription>
            Legacy router IP fields were mapped to MikroTik Router IP. Review settings and save to
            persist.
          </AlertDescription>
        </Alert>
      ) : null}

      {showRouterCacheBanner ? (
        <RouterCacheStaleBanner
          cache={routerCache}
          pending={routerCachePending}
          onRefresh={handleRouterCacheRefresh}
        />
      ) : null}

      <section
        id="syscfg-overview"
        className={cn(
          "grid scroll-mt-[3.5rem] grid-cols-1 gap-3 sm:grid-cols-2 xl:grid-cols-4",
          showSection("syscfg-overview"),
        )}
      >
        <OverviewStatusCard
          title="MikroTik Router"
          statusLabel={mikrotikStatus.label}
          statusTone={connectionToneToStatus(mikrotikStatus.tone)}
          detail={overviewRouterIdentity}
          loading={statusLoading}
        />
        <OverviewStatusCard
          title="Internet"
          statusLabel={internetStatus.label}
          statusTone={connectionToneToStatus(internetStatus.tone)}
          detail={overviewInternetIp}
          loading={statusLoading}
        />
        <OverviewStatusCard
          title={externalApOnly ? "Guest Wi-Fi" : "Wireless"}
          statusLabel={
            externalApOnly
              ? "External AP / Bridge-only"
              : wirelessConfigured
                ? "Configured"
                : "Not configured"
          }
          statusTone={externalApOnly || wirelessConfigured ? "ok" : "unknown"}
          detail={overviewWirelessSsid}
          loading={wirelessLoading}
        />
        <OverviewStatusCard
          title="System Health"
          statusLabel={healthOverview.label}
          statusTone={healthOverview.tone}
          detail={overviewHealthDetail}
          loading={healthLoading}
        />
      </section>

      <ConfigCard
        id="syscfg-network"
        title="Network"
        description="Configure the ESP32/network interface used by Renz-Fi."
        className={showSection("syscfg-network")}
      >
        {wifiConfigError ? (
          <div className="space-y-2 text-[13px] text-muted-foreground">
            <p>Unable to retrieve network configuration.</p>
            <Button
              type="button"
              size="sm"
              variant="outline"
              onClick={() => void refetchWifiConfig()}
            >
              Retry
            </Button>
          </div>
        ) : wifiConfigLoading ? (
          <div className="grid grid-cols-1 gap-3 sm:grid-cols-2">
            {Array.from({ length: 6 }).map((_, i) => (
              <Skeleton key={i} className="h-16 w-full" />
            ))}
          </div>
        ) : (
          <>
            <ConfigFormGrid>
              <ConfigField label="Mode">
                <Select
                  value={networkMode}
                  onValueChange={(value: "dhcp" | "static") => setNetworkMode(value)}
                  disabled={wifiConfigLoading}
                >
                  <SelectTrigger className={inputClass}>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="dhcp">DHCP</SelectItem>
                    <SelectItem value="static">Static</SelectItem>
                  </SelectContent>
                </Select>
              </ConfigField>
              {networkMode === "dhcp" ? (
                <>
                  <ConfigField label="IP">
                    <Input
                      value={current?.ip || systemHealth?.ethernet?.ip || "—"}
                      readOnly
                      className={readOnlyInputClass}
                    />
                  </ConfigField>
                  <ConfigField label="Gateway">
                    <Input
                      value={current?.gateway || systemHealth?.ethernet?.gateway || "—"}
                      readOnly
                      className={readOnlyInputClass}
                    />
                  </ConfigField>
                  <ConfigField label="Mask">
                    <Input
                      value={current?.netmask || systemHealth?.ethernet?.netmask || "—"}
                      readOnly
                      className={readOnlyInputClass}
                    />
                  </ConfigField>
                  <ConfigField label="DNS">
                    <Input
                      value={current?.dns || systemHealth?.ethernet?.dns || "—"}
                      readOnly
                      className={readOnlyInputClass}
                    />
                  </ConfigField>
                </>
              ) : (
                <>
                  <ConfigField label="IP">
                    <Input
                      value={staticFields.ip}
                      onChange={(e) => setStaticFields((prev) => ({ ...prev, ip: e.target.value }))}
                      placeholder="10.40.0.2"
                      className={inputClass}
                    />
                  </ConfigField>
                  <ConfigField label="Gateway">
                    <Input
                      value={staticFields.gateway}
                      onChange={(e) =>
                        setStaticFields((prev) => ({ ...prev, gateway: e.target.value }))
                      }
                      placeholder={DEFAULT_MIKROTIK_IP}
                      className={inputClass}
                    />
                  </ConfigField>
                  <ConfigField label="Mask">
                    <Input
                      value={staticFields.mask}
                      onChange={(e) =>
                        setStaticFields((prev) => ({ ...prev, mask: e.target.value }))
                      }
                      placeholder="255.255.255.0"
                      className={inputClass}
                    />
                  </ConfigField>
                  <ConfigField label="DNS">
                    <Input
                      value={staticFields.dns}
                      onChange={(e) =>
                        setStaticFields((prev) => ({ ...prev, dns: e.target.value }))
                      }
                      placeholder="8.8.8.8"
                      className={inputClass}
                    />
                  </ConfigField>
                </>
              )}
              <ConfigField label="MAC">
                <Input
                  value={current?.mac || systemHealth?.ethernet?.mac || "—"}
                  readOnly
                  className={readOnlyInputClass}
                />
              </ConfigField>
            </ConfigFormGrid>
            <Button
              size="sm"
              onClick={() => saveNetworkMutation.mutate()}
              disabled={saveNetworkMutation.isPending || wifiConfigLoading}
            >
              {saveNetworkMutation.isPending ? (
                <Loader2 className="h-4 w-4 animate-spin" />
              ) : (
                <Save className="h-4 w-4" />
              )}
              Save
            </Button>
          </>
        )}
      </ConfigCard>

      <div
        id="syscfg-wireless"
        className={cn(
          "grid scroll-mt-[3.5rem] grid-cols-1 gap-4 lg:grid-cols-2",
          showSection("syscfg-wireless"),
        )}
      >
        <WirelessConfigurationSummary
          data={wirelessData}
          loading={wirelessLoading || wirelessFetching}
          error={Boolean(wirelessError)}
          frequencyFallback={routerCache?.productionNetwork?.frequency}
        />

        <ConfigCard title="Wireless Settings" className="h-full">
          {wirelessData?.configured === false ? (
            <p className="text-xs text-muted-foreground">
              Wireless is not configured yet. Complete the setup wizard first.
            </p>
          ) : null}
          <ConfigField label="SSID">
            <Input
              value={wirelessForm.ssid}
              onChange={(e) => setWirelessForm((prev) => ({ ...prev, ssid: e.target.value }))}
              placeholder={wirelessLoading ? "Loading cached wireless..." : ""}
              disabled={wirelessLoading}
              className={inputClass}
            />
          </ConfigField>
          <ConfigField label="Password">
            <Input
              type="password"
              value={openWireless ? "" : wirelessForm.password}
              onChange={(e) => setWirelessForm((prev) => ({ ...prev, password: e.target.value }))}
              placeholder={openWireless ? "Open network — password not used" : "WiFi password"}
              disabled={wirelessLoading || openWireless}
              readOnly={openWireless}
              className={inputClass}
            />
          </ConfigField>
          <ConfigField label="Security">
            <Input value={securityDisplay} readOnly className={readOnlyInputClass} />
          </ConfigField>
          {wirelessData?.error ? (
            <p className="text-xs text-amber-600 dark:text-amber-400">{wirelessData.error}</p>
          ) : null}
          {wirelessError ? (
            <p className="text-xs text-red-600 dark:text-red-400">
              Could not load cached wireless settings.
            </p>
          ) : null}
          <Button
            size="sm"
            onClick={() => saveWirelessMutation.mutate()}
            disabled={
              saveWirelessMutation.isPending ||
              wirelessLoading ||
              !wirelessForm.ssid ||
              wirelessData?.configured === false
            }
          >
            {saveWirelessMutation.isPending ? (
              <Loader2 className="h-4 w-4 animate-spin" />
            ) : (
              <Save className="h-4 w-4" />
            )}
            Save
          </Button>
        </ConfigCard>
      </div>

      <ConfigCard
        id="syscfg-hotspot"
        title="Bandwidth"
        description="Router API, HotSpot profiles and configured rate limits"
        className={showSection("syscfg-hotspot")}
      >
        {configError ? (
          <div className="space-y-2 text-[13px] text-muted-foreground">
            <p>Unable to retrieve bandwidth settings.</p>
          </div>
        ) : null}
        <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
          <div className="space-y-3 rounded-[14px] border bg-muted/10 p-4">
            <h4 className="text-[13px] font-semibold">Router Connection</h4>
            <ConfigField
              label="Router IP Address"
              hint="MikroTik RouterOS API address (port 8728). Use the LAN IP on the ESP32 Ethernet segment (for example 10.10.10.1), not the guest hotspot gateway."
            >
              <Input
                value={form.host}
                onChange={(e) => updateField("host", e.target.value)}
                placeholder={DEFAULT_MIKROTIK_IP}
                disabled={configLoading}
                autoComplete="off"
                inputMode="decimal"
                className={inputClass}
              />
            </ConfigField>
          </div>
          <div className="space-y-3 rounded-[14px] border bg-muted/10 p-4">
            <h4 className="text-[13px] font-semibold">Router Credentials</h4>
            <ConfigField label="Router Username">
              <Input
                value={form.username}
                onChange={(e) => updateField("username", e.target.value)}
                disabled={configLoading}
                className={inputClass}
              />
            </ConfigField>
            <ConfigField label="Router Password">
              <Input
                type="password"
                value={form.password}
                onChange={(e) => updateField("password", e.target.value)}
                placeholder={
                  form.passwordConfigured
                    ? "Leave blank to keep current password"
                    : "Enter API password"
                }
                disabled={configLoading}
                className={inputClass}
              />
            </ConfigField>
            <div className="space-y-1.5">
              <div className="flex items-center justify-between gap-2">
                <Label className="text-[11px] font-medium text-muted-foreground">
                  Default Profile
                </Label>
                <Button
                  type="button"
                  size="sm"
                  variant="ghost"
                  className="h-7 px-2 text-xs"
                  disabled={
                    profilesFetching || saveMutation.isPending || rateLimitMutation.isPending
                  }
                  onClick={() => {
                    void (async () => {
                      try {
                        await routerApi.refreshProfiles();
                        await refreshProductionRouterViews(queryClient);
                        toast.success("Profiles refreshed from RouterOS");
                      } catch (err) {
                        toast.error(
                          err instanceof Error
                            ? err.message
                            : "Failed to refresh profiles — check router credentials",
                        );
                      }
                    })();
                  }}
                >
                  {profilesFetching ? (
                    <Loader2 className="h-3.5 w-3.5 animate-spin" />
                  ) : (
                    <RefreshCw className="h-3.5 w-3.5" />
                  )}
                  Refresh Profiles
                </Button>
              </div>
              <Select
                value={selectedProfile}
                onValueChange={(value) => updateField("profile", value)}
                disabled={configLoading || profilesLoading}
              >
                <SelectTrigger className={inputClass}>
                  <SelectValue
                    placeholder={
                      profilesLoading
                        ? "Loading profiles..."
                        : profileOptions.length
                          ? "Select hotspot profile"
                          : "No profiles loaded"
                    }
                  />
                </SelectTrigger>
                <SelectContent>
                  {profileOptions.map((profile) => (
                    <SelectItem key={profile} value={profile}>
                      {profile}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
              {profileOptions.length === 0 &&
              !profilesLoading &&
              !form.passwordConfigured &&
              !form.password.trim() ? (
                <p className="text-xs text-amber-600 dark:text-amber-400">
                  Router credentials required to load profiles.
                </p>
              ) : null}
              {profilesData?.error && profileOptions.length === 0 ? (
                <p className="text-xs text-amber-600 dark:text-amber-400">{profilesData.error}</p>
              ) : null}
              {profilesData?.truncated ? (
                <p className="text-xs text-amber-600 dark:text-amber-400">
                  RouterOS returned more profiles than could be listed at once.
                </p>
              ) : null}
              {profilesError ? (
                <p className="text-xs text-red-600 dark:text-red-400">
                  Could not load profiles from the appliance.
                </p>
              ) : null}
            </div>
          </div>
        </div>

        {profileDetails.length > 0 ? (
          <div className="space-y-2">
            <Label className="text-xs">Available Profiles</Label>
            {profilesData?.stale ? (
              <p className="text-xs text-muted-foreground">
                Cached profile list may be stale — run Test Connection or Synchronize to refresh.
              </p>
            ) : null}
            <div className="rounded-md border divide-y">
              {profileDetails.map((profile) => (
                <div key={profile.name} className="flex flex-col gap-2 px-3 py-2 text-sm">
                  <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-1">
                    <div className="min-w-0">
                      <div className="font-medium truncate">{profile.name}</div>
                      <div className="text-xs text-muted-foreground">
                        Rate Limit: {formatRateLimit(profile.rateLimit)}
                      </div>
                    </div>
                    <div className="flex items-center gap-2 shrink-0">
                      <ConfigStatusBadge label="Available" tone="ok" />
                      <Button
                        type="button"
                        size="sm"
                        variant="outline"
                        className="h-7 text-xs"
                        disabled={rateLimitMutation.isPending}
                        onClick={() => {
                          setEditingRateLimitName(profile.name);
                          setEditingRateLimitValue(profile.rateLimit?.trim() || "");
                        }}
                      >
                        Edit Rate Limit
                      </Button>
                    </div>
                  </div>
                  {editingRateLimitName === profile.name ? (
                    <div className="rounded-md border bg-muted/20 p-2 space-y-2">
                      <p className="text-xs text-amber-700 dark:text-amber-400">
                        This changes the MikroTik Hotspot user profile and may affect users assigned
                        to this profile.
                      </p>
                      <div className="flex flex-wrap items-end gap-2">
                        <div className="space-y-1 grow min-w-[10rem]">
                          <Label className="text-xs">rate-limit (rx/tx)</Label>
                          <Input
                            value={editingRateLimitValue}
                            onChange={(e) => setEditingRateLimitValue(e.target.value)}
                            placeholder="e.g. 10M/5M"
                            disabled={rateLimitMutation.isPending}
                          />
                        </div>
                        <Button
                          type="button"
                          size="sm"
                          disabled={rateLimitMutation.isPending}
                          onClick={() =>
                            rateLimitMutation.mutate({
                              name: profile.name,
                              rateLimit: editingRateLimitValue.trim(),
                            })
                          }
                        >
                          {rateLimitMutation.isPending ? (
                            <Loader2 className="h-3.5 w-3.5 animate-spin" />
                          ) : (
                            "Apply"
                          )}
                        </Button>
                        <Button
                          type="button"
                          size="sm"
                          variant="ghost"
                          disabled={rateLimitMutation.isPending}
                          onClick={() => {
                            setEditingRateLimitName(null);
                            setEditingRateLimitValue("");
                          }}
                        >
                          Cancel
                        </Button>
                      </div>
                    </div>
                  ) : null}
                </div>
              ))}
            </div>
          </div>
        ) : null}
        <div className="flex flex-col gap-2 sm:flex-row sm:flex-wrap sm:items-center">
          <Button
            size="sm"
            onClick={() => testMutation.mutate()}
            disabled={testMutation.isPending || configLoading || !form.host?.trim()}
          >
            {testMutation.isPending ? (
              <Loader2 className="h-4 w-4 animate-spin" />
            ) : (
              <Plug className="h-4 w-4" />
            )}
            Test Connection
          </Button>
          <Button
            size="sm"
            variant="outline"
            onClick={() => saveMutation.mutate()}
            disabled={saveMutation.isPending || configLoading || !form.host?.trim()}
          >
            {saveMutation.isPending ? (
              <Loader2 className="h-4 w-4 animate-spin" />
            ) : (
              <Save className="h-4 w-4" />
            )}
            Save
          </Button>
        </div>
        {testResult ? (
          <div className="rounded-md border bg-muted/20 p-3 space-y-2">
            <p
              className={cn(
                "text-sm font-medium",
                testResult.ok
                  ? "text-emerald-600 dark:text-emerald-400"
                  : "text-red-600 dark:text-red-400",
              )}
            >
              {testResult.summary}
            </p>
            <ul className="space-y-1.5">
              {(testResult.steps ?? []).map((step) => (
                <li key={step.id} className="flex items-start gap-2 text-xs">
                  {step.ok ? (
                    <CheckCircle2 className="h-3.5 w-3.5 text-emerald-600 shrink-0 mt-0.5" />
                  ) : (
                    <XCircle className="h-3.5 w-3.5 text-red-600 shrink-0 mt-0.5" />
                  )}
                  <span>
                    <span className="font-medium">{step.label}</span>
                    <span className="text-muted-foreground"> — {step.message}</span>
                  </span>
                </li>
              ))}
            </ul>
            {!testResult.steps?.length ? (
              <p className="text-xs text-muted-foreground flex items-center gap-1">
                <MinusCircle className="h-3.5 w-3.5" />
                Detailed step results are not available from this firmware build.
              </p>
            ) : null}
          </div>
        ) : null}
      </ConfigCard>

      <ConfigCard
        id="syscfg-storage"
        title="Storage & Firmware"
        description="Running firmware, build metadata, and SD card health."
        className={showSection("syscfg-storage")}
      >
        <SystemBuildInfo />
        <StorageHealthCard variant="console" />
      </ConfigCard>

      <ConfigCard
        id="syscfg-router"
        title="Router Status"
        className={showSection("syscfg-router")}
        actions={
          statusLoading || cacheFetching ? null : (
            <ConfigStatusBadge
              label={mikrotikStatus.label}
              tone={connectionToneToStatus(mikrotikStatus.tone)}
            />
          )
        }
      >
        {cacheError && !routerCache && statusError ? (
          <div className="space-y-2 text-[13px] text-muted-foreground">
            <p>Unable to retrieve router information.</p>
            <Button
              type="button"
              size="sm"
              variant="outline"
              onClick={() => {
                void refetchCache();
                void refetchStatus();
              }}
            >
              Retry
            </Button>
          </div>
        ) : (
          <>
            <div className="grid grid-cols-1 gap-4 lg:grid-cols-2">
              <div className="space-y-2">
                <h4 className="text-[13px] font-semibold">Connectivity</h4>
                <ConnectionStatusList items={connectionItems} />
                {!systemStatus?.wan?.known && !statusLoading ? (
                  <p className="text-xs text-muted-foreground">
                    Internet reflects MikroTik WAN upstream (not captive portal session state). Run
                    Synchronize Router or Refresh Router Information to probe ether1-WAN.
                  </p>
                ) : null}
              </div>
              <div className="space-y-2">
                <h4 className="text-[13px] font-semibold">Router information</h4>
                <div className="rounded-md border bg-muted/20 px-3 pr-4">
                  <InfoRow
                    label="Router Identity"
                    value={routerCache?.identity || testResult?.identity || "—"}
                    mono
                  />
                  <InfoRow
                    label="RouterOS Version"
                    value={routerOsSnapshot?.version || routerCache?.routerOsVersion || "—"}
                    mono
                  />
                  <InfoRow
                    label="Last Synchronization"
                    value={routerCacheLastSyncLabel(routerCache)}
                    mono
                  />
                  <InfoRow
                    label="Cache Age"
                    value={formatRouterCacheAge(routerCache?.cacheAgeSeconds)}
                    mono
                  />
                  <InfoRow
                    label="Provision Status"
                    value={routerCacheProvisionStatusLabel(routerCache)}
                  />
                  <InfoRow
                    label="Network Mode"
                    value={
                      externalApOnly ? "External Access Point / Bridge-only" : "MikroTik wireless"
                    }
                  />
                  <InfoRow
                    label="Production Wi-Fi"
                    value={routerCacheProductionWifiLabel(
                      routerCache,
                      "Healthy",
                      externalApOnly
                        ? productionNetworkReasonLabel("external-ap-topology")
                        : productionNetworkReasonLabel(routerCache?.productionNetwork?.reason),
                      externalApOnly,
                    )}
                  />
                  <InfoRow
                    label="Production SSID"
                    value={
                      externalApOnly
                        ? "External AP (configured separately)"
                        : routerCache?.ssid || routerCache?.productionNetwork?.ssid || "—"
                    }
                    mono={!externalApOnly}
                  />
                  <InfoRow
                    label="Hotspot Profile"
                    value={routerCache?.hotspotProfile || form.profile || "—"}
                  />
                  <InfoRow
                    label="CPU"
                    value={routerOsSnapshot?.cpuLoad ? `${routerOsSnapshot.cpuLoad}%` : "—"}
                    mono
                  />
                  <InfoRow
                    label="Memory"
                    value={formatMemory(
                      routerOsSnapshot?.freeMemory,
                      routerOsSnapshot?.totalMemory,
                    )}
                    mono
                  />
                  <InfoRow label="Uptime" value={formatUptime(routerOsSnapshot?.uptime)} mono />
                </div>
                {!routerOsSnapshot && !routerCache?.identity ? (
                  <p className="text-xs text-muted-foreground">
                    Run Test Connection or Synchronize Router to load live RouterOS metrics.
                  </p>
                ) : null}
              </div>
            </div>
            <div className="flex flex-col gap-2 sm:flex-row sm:flex-wrap sm:items-center">
              <Button
                type="button"
                size="sm"
                disabled={routerCachePending}
                onClick={() => syncRouterMutation.mutate()}
              >
                {syncRouterMutation.isPending ? (
                  <Loader2 className="h-4 w-4 animate-spin" />
                ) : (
                  <RefreshCw className="h-4 w-4" />
                )}
                Synchronize Router
              </Button>
              <Button
                type="button"
                size="sm"
                variant="outline"
                disabled={routerCachePending}
                onClick={handleRouterCacheRefresh}
              >
                {refreshCacheMutation.isPending ? (
                  <Loader2 className="h-4 w-4 animate-spin" />
                ) : (
                  <RefreshCw className="h-4 w-4" />
                )}
                Refresh Router Information
              </Button>
            </div>
          </>
        )}
      </ConfigCard>
    </div>
  );
}
