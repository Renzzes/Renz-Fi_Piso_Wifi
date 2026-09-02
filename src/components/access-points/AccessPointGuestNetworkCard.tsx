import { ConfigCard } from "@/components/system-config/ConfigCard";
import { InfoRow } from "@/components/system-config/InfoRow";
import { Skeleton } from "@/components/ui/skeleton";
import type { SystemStatus } from "@/types/api";

type GuestNetworkProvisioning = NonNullable<SystemStatus["networkProvisioning"]>;

function displayValue(value: string | undefined): string {
  const trimmed = value?.trim();
  return trimmed ? trimmed : "Unavailable";
}

export function AccessPointGuestNetworkCard({
  provisioning,
  loading,
}: {
  provisioning?: GuestNetworkProvisioning;
  loading?: boolean;
}) {
  const dns = provisioning?.guestGateway?.trim() || undefined;

  return (
    <ConfigCard
      title="Guest Network Configuration"
      description="Provisioned MikroTik guest network values (from appliance configuration)."
    >
      {loading ? (
        <div className="space-y-2">
          {Array.from({ length: 4 }).map((_, i) => (
            <Skeleton key={i} className="h-8 w-full" />
          ))}
        </div>
      ) : (
        <div className="rounded-md border bg-muted/20 px-3">
          <InfoRow label="Network" value={displayValue(provisioning?.guestNetwork)} mono />
          <InfoRow label="Gateway" value={displayValue(provisioning?.guestGateway)} mono />
          <InfoRow label="DNS" value={displayValue(dns)} mono />
          <InfoRow label="DHCP Pool" value={displayValue(provisioning?.dhcpPool)} mono />
          {provisioning?.guestBridgeName ? (
            <InfoRow label="Guest Bridge" value={provisioning.guestBridgeName} mono />
          ) : null}
        </div>
      )}
    </ConfigCard>
  );
}
