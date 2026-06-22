import { useState, useEffect, useMemo } from "react";
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
  DEFAULT_ESP32_IP,
  DEFAULT_MIKROTIK_IP,
  normalizeRouterConfig,
  routerConfigNeedsMigration,
  toRouterSavePayload,
  toRouterTestPayload,
} from "@/lib/routerConfig";
import {
  hotspotServiceStatusDisplay,
  internetReachabilityDisplay,
  mikrotikApiStatusDisplay,
} from "@/lib/systemConfigurationStatus";
import { adminBuildId } from "@/lib/buildInfo";
import { cn } from "@/lib/utils";
import { toast } from "sonner";

function defaultFormState(): RouterConfig {
  return normalizeRouterConfig(null);
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
    steps,
    summary: result.summary ?? (result.ok ? "All checks passed" : "One or more checks failed"),
  };
}

export default function SystemConfigurationPage() {
  const queryClient = useQueryClient();
  const [form, setForm] = useState<RouterConfig>(defaultFormState);
  const [testResult, setTestResult] = useState<RouterTestResult | null>(null);
  const [migrationApplied, setMigrationApplied] = useState(false);

  const { data: rawConfig, isLoading: configLoading } = useQuery({
    queryKey: ["router", "settings"],
    queryFn: () => routerApi.settings(),
  });

  const { data: systemStatus, isLoading: statusLoading } = useQuery({
    queryKey: ["system", "status"],
    queryFn: () => systemApi.status(),
    refetchInterval: 30_000,
  });

  const { data: networkStatus } = useQuery({
    queryKey: ["system", "network"],
    queryFn: () => systemApi.network(),
    staleTime: 60_000,
  });

  const esp32Ip =
    networkStatus?.ethernet?.ip?.trim() ||
    networkStatus?.ip?.trim() ||
    DEFAULT_ESP32_IP;

  useEffect(() => {
    if (!rawConfig) return;
    const normalized = normalizeRouterConfig(rawConfig);
    setForm(normalized);
    if (routerConfigNeedsMigration(rawConfig)) {
      setMigrationApplied(true);
    }
  }, [rawConfig]);

  const {
    data: profilesData,
    isLoading: profilesLoading,
    isFetching: profilesFetching,
    refetch: refetchProfiles,
    error: profilesError,
  } = useQuery({
    queryKey: ["router", "profiles"],
    queryFn: () => routerApi.profiles(),
    staleTime: 60_000,
    retry: 1,
  });

  const profileOptions = useMemo(() => {
    return Array.isArray(profilesData?.profiles) ? profilesData.profiles : [];
  }, [profilesData?.profiles]);

  useEffect(() => {
    console.log("Router profiles response:", profilesData);
  }, [profilesData]);

  useEffect(() => {
    console.log("Profile options:", profileOptions);
  }, [profileOptions]);

  // When RouterOS profiles load, replace stale placeholder (e.g. "default") with a real profile.
  useEffect(() => {
    if (profileOptions.length === 0) return;
    setForm((prev) => {
      if (profileOptions.includes(prev.profile)) return prev;
      if (prev.profile && prev.profile !== "default") return prev;
      return { ...prev, profile: profileOptions[0] };
    });
  }, [profileOptions]);

  const selectedProfile =
    profileOptions.length === 0
      ? form.profile || undefined
      : profileOptions.includes(form.profile)
        ? form.profile
        : profileOptions[0];

  const saveMutation = useMutation({
    mutationFn: () => routerApi.save(toRouterSavePayload(form)),
    onSuccess: async () => {
      void queryClient.invalidateQueries({ queryKey: ["router", "settings"] });
      void queryClient.invalidateQueries({ queryKey: ["system", "status"] });
      await refetchProfiles();
      setForm((prev) => ({ ...prev, password: "" }));
      setMigrationApplied(false);
      toast.success("System configuration saved");
    },
    onError: () => toast.error("Failed to save configuration"),
  });

  const testMutation = useMutation({
    mutationFn: () => routerApi.test(toRouterTestPayload(form)),
    onMutate: () => setTestResult(null),
    onSuccess: (result) => {
      const normalized = normalizeTestResult(result);
      setTestResult(normalized);
      void queryClient.invalidateQueries({ queryKey: ["system", "status"] });
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

  const connectionItems = useMemo(
    () => [
      {
        label: "MikroTik API",
        status: mikrotikApiStatusDisplay(systemStatus?.mikrotik, statusLoading),
      },
      {
        label: "Hotspot Service",
        status: hotspotServiceStatusDisplay(systemStatus?.hotspot, statusLoading),
      },
      {
        label: "Internet Reachability",
        status: internetReachabilityDisplay(systemStatus?.internet, statusLoading),
      },
    ],
    [systemStatus, statusLoading],
  );

  const updateField = <K extends keyof RouterConfig>(key: K, value: RouterConfig[K]) => {
    setForm((prev) => ({ ...prev, [key]: value }));
  };

  return (
    <div className="space-y-3">
      <PageHeader
        title="System Configuration"
        description="WiFi, network, and MikroTik hotspot settings for this node"
      />
      <p className="text-[10px] font-mono text-muted-foreground -mt-1">
        Admin UI build: {adminBuildId}
      </p>

      {migrationApplied ? (
        <Alert>
          <AlertTitle>Configuration migrated</AlertTitle>
          <AlertDescription>
            Legacy router IP fields were mapped to MikroTik Router IP. The previous value may have
            pointed at the ESP32 address ({DEFAULT_ESP32_IP}). Review settings and save to persist.
          </AlertDescription>
        </Alert>
      ) : null}

      <div className="grid lg:grid-cols-2 gap-3">
        <ConfigSection
          title="WiFi Settings"
          description="Customer hotspot wireless network (stored for reference and dashboard status)"
        >
          <div className="space-y-1">
            <Label className="text-xs">SSID</Label>
            <Input
              value={form.ssid ?? ""}
              onChange={(e) => updateField("ssid", e.target.value)}
              placeholder="RenzFi_PesoWifi"
              disabled={configLoading}
            />
          </div>
          <div className="space-y-1">
            <Label className="text-xs">Password</Label>
            <Input
              type="password"
              value={form.wifiPassword ?? ""}
              onChange={(e) => updateField("wifiPassword", e.target.value)}
              placeholder="Leave empty for open network"
              disabled={configLoading}
            />
            <p className="text-xs text-muted-foreground">
              WiFi security is configured on the MikroTik router. This field is stored for your
              records.
            </p>
          </div>
        </ConfigSection>

        <ConfigSection
          title="Network Settings"
          description="VLAN40 backend topology — ESP32 is the appliance; MikroTik is the gateway"
        >
          <div className="space-y-1">
            <Label className="text-xs">ESP32 IP Address</Label>
            <Input value={esp32Ip} readOnly className="bg-muted/50" />
            <p className="text-xs text-muted-foreground">
              Read-only. Set in firmware (W5500Config.h). Default: {DEFAULT_ESP32_IP}
            </p>
          </div>
          <div className="space-y-1">
            <Label className="text-xs">MikroTik Router IP</Label>
            <Input
              value={form.host}
              onChange={(e) => updateField("host", e.target.value)}
              placeholder={DEFAULT_MIKROTIK_IP}
              disabled={configLoading}
            />
            <p className="text-xs text-muted-foreground">
              RouterOS API and hotspot gateway. Default: {DEFAULT_MIKROTIK_IP}
            </p>
          </div>
        </ConfigSection>

        <ConfigSection
          title="Hotspot Settings"
          description="RouterOS API credentials and hotspot user profile for coin sessions"
        >
          <div className="grid sm:grid-cols-2 gap-3">
            <div className="space-y-1">
              <Label className="text-xs">RouterOS API Username</Label>
              <Input
                value={form.username}
                onChange={(e) => updateField("username", e.target.value)}
                disabled={configLoading}
              />
            </div>
            <div className="space-y-1">
              <Label className="text-xs">RouterOS API Password</Label>
              <Input
                type="password"
                value={form.password}
                onChange={(e) => updateField("password", e.target.value)}
                placeholder={form.passwordConfigured ? "Leave blank to keep current password" : "Enter API password"}
                disabled={configLoading}
              />
              {form.passwordConfigured ? (
                <p className="text-xs text-emerald-600 dark:text-emerald-400">
                  Password already configured
                </p>
              ) : (
                <p className="text-xs text-muted-foreground">
                  Enter the RouterOS API password once. It is stored on the device and never shown again.
                </p>
              )}
            </div>
          </div>
          <div className="space-y-1">
            <div className="flex items-center justify-between gap-2">
              <Label className="text-xs">Hotspot Profile</Label>
              <Button
                type="button"
                size="sm"
                variant="ghost"
                className="h-7 px-2 text-xs"
                disabled={profilesFetching || saveMutation.isPending}
                onClick={() => void refetchProfiles()}
              >
                {profilesFetching ? (
                  <Loader2 className="h-3.5 w-3.5 animate-spin" />
                ) : (
                  <RefreshCw className="h-3.5 w-3.5" />
                )}
                Refresh
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
            <p className="text-xs text-muted-foreground">
              Loaded from MikroTik via RouterOS API. Save credentials first, then refresh.
            </p>
            {profilesData?.error ? (
              <p className="text-xs text-amber-600 dark:text-amber-400">{profilesData.error}</p>
            ) : null}
            {profilesError ? (
              <p className="text-xs text-red-600 dark:text-red-400">
                Could not load profiles from the appliance.
              </p>
            ) : null}
          </div>
        </ConfigSection>

        <ConfigSection
          title="Connection Status"
          description="Live status from the appliance — refreshed every 30 seconds"
        >
          <ConnectionStatusList items={connectionItems} />
        </ConfigSection>
      </div>

      <ConfigSection title="Diagnostics" description="Verify RouterOS API access before saving">
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
            Save Configuration
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
    </div>
  );
}
