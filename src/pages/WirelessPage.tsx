import { Loader2 } from "lucide-react";
import { useQuery } from "@tanstack/react-query";
import { ConfigCard } from "@/components/system-config/ConfigCard";
import { ConfigStatusBadge } from "@/components/system-config/ConfigStatusBadge";
import SystemConfigurationPage from "@/pages/SystemConfigurationPage";
import { resolveWirelessCapability } from "@/lib/wirelessCapability";
import { routerApi } from "@/services/router";
import { systemApi } from "@/services/system";

/** Wireless configuration — capability-driven; external-AP-only installs show Not Applicable. */
export default function WirelessPage() {
  const { data: systemStatus, isLoading: statusLoading } = useQuery({
    queryKey: ["system", "status"],
    queryFn: () => systemApi.status(),
    staleTime: 60_000,
  });

  const { data: wirelessData, isLoading: wirelessLoading } = useQuery({
    queryKey: ["router", "wireless"],
    queryFn: () => routerApi.wireless(),
    staleTime: 60_000,
  });

  const { data: routerCache, isLoading: cacheLoading } = useQuery({
    queryKey: ["router", "cache"],
    queryFn: () => routerApi.cache(),
    staleTime: 60_000,
    refetchOnMount: true,
  });

  const capability = resolveWirelessCapability({
    wireless: wirelessData,
    networkProvisioning: systemStatus?.networkProvisioning,
    routerBoardName: routerCache?.routerOs?.boardName,
    routerIdentity: routerCache?.identity,
  });

  const loading = statusLoading || wirelessLoading || cacheLoading;

  if (loading) {
    return (
      <div className="w-full max-w-none space-y-4">
        <div>
          <h2 className="text-2xl font-semibold leading-tight">Wireless</h2>
          <p className="mt-0.5 text-[13px] text-muted-foreground">
            MikroTik wireless configuration for customer Wi-Fi.
          </p>
        </div>
        <p className="flex items-center gap-2 text-xs text-muted-foreground">
          <Loader2 className="h-3.5 w-3.5 animate-spin" aria-hidden />
          Loading wireless capability…
        </p>
      </div>
    );
  }

  if (capability === "external_ap_only") {
    return (
      <div className="w-full max-w-none space-y-4">
        <div>
          <h2 className="text-2xl font-semibold leading-tight">Wireless</h2>
          <p className="mt-0.5 text-[13px] text-muted-foreground">
            MikroTik wireless configuration for customer Wi-Fi.
          </p>
        </div>
        <ConfigCard
          title="Wireless"
          description="MikroTik wireless configuration is not applicable to this router."
        >
          <div className="flex flex-wrap items-center gap-2">
            <span className="text-[13px] text-muted-foreground">Status</span>
            <ConfigStatusBadge label="Not Available" tone="neutral" />
          </div>
          <p className="mt-3 text-[13px] text-muted-foreground leading-relaxed">
            This installation uses an external Access Point connected to the MikroTik guest bridge.
            Configure Wi-Fi on the AP&apos;s own web interface and register it under{" "}
            <strong>Access Points</strong>.
          </p>
        </ConfigCard>
      </div>
    );
  }

  if (capability === "unknown") {
    return (
      <div className="w-full max-w-none space-y-4">
        <div>
          <h2 className="text-2xl font-semibold leading-tight">Wireless</h2>
          <p className="mt-0.5 text-[13px] text-muted-foreground">
            MikroTik wireless configuration for customer Wi-Fi.
          </p>
        </div>
        <ConfigCard
          title="Wireless"
          description="Unable to determine whether this router uses MikroTik wireless."
        >
          <div className="flex flex-wrap items-center gap-2">
            <span className="text-[13px] text-muted-foreground">Status</span>
            <ConfigStatusBadge label="Unable to determine" tone="unknown" />
          </div>
          <p className="mt-3 text-[13px] text-muted-foreground leading-relaxed">
            Complete setup or run Router Sync so provisioning state can be read. Do not assume a
            wireless interface exists until capability is confirmed.
          </p>
        </ConfigCard>
      </div>
    );
  }

  return <SystemConfigurationPage fixedSection="syscfg-wireless" />;
}
