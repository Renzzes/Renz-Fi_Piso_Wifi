import { useState, useEffect, useMemo, useRef } from "react";
import { useQuery, useMutation, useQueryClient } from "@tanstack/react-query";
import { PageHeader } from "@/components/PageHeader";
import { ConfigSection } from "@/components/ConfigSection";
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
import { Plug, Save, Loader2, CheckCircle2, XCircle, MinusCircle, RefreshCw } from "lucide-react";
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
} from "@/lib/systemConfigurationStatus";
import { SystemBuildInfo } from "@/components/SystemBuildInfo";
import { RouterCacheStaleBanner } from "@/components/RouterCacheStaleBanner";
import { WirelessConfigurationSummary } from "@/components/WirelessConfigurationSummary";
import { StorageHealthCard } from "@/components/StorageHealthCard";
import { cn } from "@/lib/utils";
import { formatRouterCacheAge, routerCacheLastSyncLabel, routerCacheProductionWifiLabel, routerCacheProvisionStatusLabel } from "@/lib/routerCacheStatus";
import {
  productionNetworkReasonLabel,
} from "@/lib/productionNetworkReason";
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
      result.ok ??
      Boolean(result.connected && result.authenticated && result.profileFound);
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

export default function SystemConfigurationPage() {
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
  const profileRefreshAttempted = useRef(false);

  const { data: rawConfig, isLoading: configLoading } = useQuery({
    queryKey: ["router", "settings"],
    queryFn: () => routerApi.settings(),
    ...CONFIG_QUERY_OPTIONS,
  });

  const {
    data: routerCache,
    isFetching: cacheFetching,
    refetch: refetchCache,
  } = useQuery({
    queryKey: ["router", "cache"],
    queryFn: () => routerApi.cache(),
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
    ...CONFIG_QUERY_OPTIONS,
  });

  const { data: systemStatus, isLoading: statusLoading } = useQuery({
    queryKey: ["system", "status"],
    queryFn: () => systemApi.status(),
    refetchInterval: 30_000,
  });

  const { data: wifiConfig, isLoading: wifiConfigLoading } = useQuery({
    queryKey: ["system", "wifiConfig"],
    queryFn: () => systemApi.wifiConfig(),
    staleTime: 5_000,
    refetchOnMount: true,
  });

  const { data: systemHealth, isLoading: healthLoading } = useQuery({
    queryKey: ["system", "health"],
    queryFn: () => healthApi.get(),
    refetchInterval: 30_000,
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
    const canRefresh = Boolean(form.host?.trim()) &&
      (form.passwordConfigured || form.password.trim().length > 0 || form.username.trim().length > 0);
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
      toast.error(
        err instanceof Error ? err.message : "Failed to refresh router information",
      ),
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
  const routerCachePending =
    refreshCacheMutation.isPending ||
    syncRouterMutation.isPending ||
    cacheFetching ||
    storageRecovering;

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
      toast.success("Hotspot settings saved");
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
      toast.success(result.rebootRequired ? "Network settings saved — reboot to apply" : "Network settings saved");
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
            result.profileDetails ??
            result.profiles.map((name) => ({ name, rateLimit: "" })),
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

  return (
    <div className="space-y-3">
      <PageHeader title="System Configuration" />

      <SystemBuildInfo />

      {migrationApplied ? (
        <Alert>
          <AlertTitle>Configuration migrated</AlertTitle>
          <AlertDescription>
            Legacy router IP fields were mapped to MikroTik Router IP. Review settings and save to
            persist.
          </AlertDescription>
        </Alert>
      ) : null}

      <RouterCacheStaleBanner
        cache={routerCache}
        pending={routerCachePending}
        onRefresh={handleRouterCacheRefresh}
      />

      <div className="grid lg:grid-cols-2 gap-3">
        <StorageHealthCard className="lg:col-span-2" />

        <WirelessConfigurationSummary
          data={wirelessData}
          loading={wirelessLoading || wirelessFetching}
          frequencyFallback={routerCache?.productionNetwork?.frequency}
        />

        <ConfigSection
          title="Wireless Settings"
          panelId="syscfg-wireless"
          summary="SSID / security settings"
        >
          {wirelessData?.configured === false ? (
            <p className="text-xs text-muted-foreground">
              Wireless is not configured yet. Complete the setup wizard first.
            </p>
          ) : null}
          <div className="space-y-1">
            <Label className="text-xs">SSID</Label>
            <Input
              value={wirelessForm.ssid}
              onChange={(e) => setWirelessForm((prev) => ({ ...prev, ssid: e.target.value }))}
              placeholder={wirelessLoading ? "Loading cached wireless..." : ""}
              disabled={wirelessLoading}
            />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Password</Label>
            <Input
              type="password"
              value={openWireless ? "" : wirelessForm.password}
              onChange={(e) => setWirelessForm((prev) => ({ ...prev, password: e.target.value }))}
              placeholder={openWireless ? "Open network — password not used" : "WiFi password"}
              disabled={wirelessLoading || openWireless}
              readOnly={openWireless}
            />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Security</Label>
            <Input value={securityDisplay} readOnly className="bg-muted/50" />
          </div>
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
        </ConfigSection>

        <ConfigSection
          title="Network"
          panelId="syscfg-network"
          summary="Router connection and LAN"
        >
          <div className="space-y-1">
            <Label className="text-xs">Mode</Label>
            <Select
              value={networkMode}
              onValueChange={(value: "dhcp" | "static") => setNetworkMode(value)}
              disabled={wifiConfigLoading}
            >
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value="dhcp">DHCP</SelectItem>
                <SelectItem value="static">Static</SelectItem>
              </SelectContent>
            </Select>
          </div>

          {networkMode === "dhcp" ? (
            <div className="grid sm:grid-cols-2 gap-3">
              <div className="space-y-1">
                <Label className="text-xs">IP</Label>
                <Input
                  value={
                    current?.ip ||
                    systemHealth?.ethernet?.ip ||
                    "—"
                  }
                  readOnly
                  className="bg-muted/50"
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs">Gateway</Label>
                <Input
                  value={
                    current?.gateway ||
                    systemHealth?.ethernet?.gateway ||
                    "—"
                  }
                  readOnly
                  className="bg-muted/50"
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs">Mask</Label>
                <Input
                  value={
                    current?.netmask ||
                    systemHealth?.ethernet?.netmask ||
                    "—"
                  }
                  readOnly
                  className="bg-muted/50"
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs">DNS</Label>
                <Input
                  value={
                    current?.dns ||
                    systemHealth?.ethernet?.dns ||
                    "—"
                  }
                  readOnly
                  className="bg-muted/50"
                />
              </div>
            </div>
          ) : (
            <div className="grid sm:grid-cols-2 gap-3">
              <div className="space-y-1">
                <Label className="text-xs">IP</Label>
                <Input
                  value={staticFields.ip}
                  onChange={(e) => setStaticFields((prev) => ({ ...prev, ip: e.target.value }))}
                  placeholder="10.40.0.2"
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs">Gateway</Label>
                <Input
                  value={staticFields.gateway}
                  onChange={(e) => setStaticFields((prev) => ({ ...prev, gateway: e.target.value }))}
                  placeholder={DEFAULT_MIKROTIK_IP}
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs">Mask</Label>
                <Input
                  value={staticFields.mask}
                  onChange={(e) => setStaticFields((prev) => ({ ...prev, mask: e.target.value }))}
                  placeholder="255.255.255.0"
                />
              </div>
              <div className="space-y-1">
                <Label className="text-xs">DNS</Label>
                <Input
                  value={staticFields.dns}
                  onChange={(e) => setStaticFields((prev) => ({ ...prev, dns: e.target.value }))}
                  placeholder="8.8.8.8"
                />
              </div>
            </div>
          )}

          <div className="space-y-1">
            <Label className="text-xs">MAC</Label>
            <Input
              value={current?.mac || systemHealth?.ethernet?.mac || "—"}
              readOnly
              className="bg-muted/50"
            />
          </div>

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
        </ConfigSection>

        <ConfigSection
          title="Hotspot"
          panelId="syscfg-hotspot"
          summary="Profiles and rate limits"
        >
          <div className="grid sm:grid-cols-2 gap-3">
            <div className="space-y-1">
              <Label className="text-xs">Router Username</Label>
              <Input
                value={form.username}
                onChange={(e) => updateField("username", e.target.value)}
                disabled={configLoading}
              />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">Router Password</Label>
              <Input
                type="password"
                value={form.password}
                onChange={(e) => updateField("password", e.target.value)}
                placeholder={form.passwordConfigured ? "Leave blank to keep current password" : "Enter API password"}
                disabled={configLoading}
              />
            </div>
          </div>
          <div className="space-y-1">
            <div className="flex items-center justify-between gap-2">
              <Label className="text-xs">Default Profile</Label>
              <Button
                type="button"
                size="sm"
                variant="ghost"
                className="h-7 px-2 text-xs"
                disabled={profilesFetching || saveMutation.isPending || rateLimitMutation.isPending}
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
              <SelectTrigger>
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
                  <div
                    key={profile.name}
                    className="flex flex-col gap-2 px-3 py-2 text-sm"
                  >
                    <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-1">
                      <div className="min-w-0">
                        <div className="font-medium truncate">{profile.name}</div>
                        <div className="text-xs text-muted-foreground">
                          Rate Limit: {formatRateLimit(profile.rateLimit)}
                        </div>
                      </div>
                      <div className="flex items-center gap-2 shrink-0">
                        <span className="text-xs text-muted-foreground">Status: Available</span>
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
                          This changes the MikroTik Hotspot user profile and may affect users
                          assigned to this profile.
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
          <div className="flex flex-wrap items-center gap-2">
            <Button
              size="sm"
              onClick={() => testMutation.mutate()}
              disabled={testMutation.isPending || configLoading}
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
              disabled={saveMutation.isPending || configLoading}
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
                  testResult.ok ? "text-emerald-600 dark:text-emerald-400" : "text-red-600 dark:text-red-400",
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
        </ConfigSection>

        <ConfigSection
          title="Status"
          panelId="syscfg-status"
          summary="Live router / WAN status"
        >
          <ConnectionStatusList items={connectionItems} />
          <div className="flex flex-wrap items-center gap-2 pb-2">
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
          <div className="rounded-md border bg-muted/30 px-3">
            {[
              ["Router Identity", routerCache?.identity || testResult?.identity || "—"],
              [
                "RouterOS Version",
                routerOsSnapshot?.version || routerCache?.routerOsVersion || "—",
              ],
              ["Last Synchronization", routerCacheLastSyncLabel(routerCache)],
              ["Cache Age", formatRouterCacheAge(routerCache?.cacheAgeSeconds)],
              ["Provision Status", routerCacheProvisionStatusLabel(routerCache)],
              [
                "Production Wi-Fi",
                routerCacheProductionWifiLabel(
                  routerCache,
                  "Healthy",
                  productionNetworkReasonLabel(routerCache?.productionNetwork?.reason),
                ),
              ],
              [
                "Production SSID",
                routerCache?.productionNetwork?.ssid || routerCache?.ssid || "—",
              ],
              ["Hotspot Profile", routerCache?.hotspotProfile || form.profile || "—"],
              [
                "CPU",
                routerOsSnapshot?.cpuLoad ? `${routerOsSnapshot.cpuLoad}%` : "—",
              ],
              ["Memory", formatMemory(routerOsSnapshot?.freeMemory, routerOsSnapshot?.totalMemory)],
              ["Uptime", formatUptime(routerOsSnapshot?.uptime)],
            ].map(([label, value]) => (
              <div
                key={label}
                className="flex items-center justify-between py-2 border-b last:border-0 text-sm"
              >
                <span className="text-muted-foreground">{label}</span>
                <span className="font-medium">{value}</span>
              </div>
            ))}
          </div>
          {!routerOsSnapshot && !routerCache?.identity ? (
            <p className="text-xs text-muted-foreground">
              Run Test Connection or Synchronize Router to load live RouterOS metrics.
            </p>
          ) : null}
        </ConfigSection>
      </div>
    </div>
  );
}
