import { useQuery } from "@tanstack/react-query";
import { ChevronDown, Loader2 } from "lucide-react";
import { Alert, AlertDescription } from "@/components/ui/alert";
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from "@/components/ui/collapsible";
import { InfoRow } from "@/components/system-config/InfoRow";
import { formatBuildDate, formatBuildNumber } from "@/lib/buildMetadataDisplay";
import { fetchApplianceBuildSnapshot } from "@/services/applianceBuild";
import { firmwareApi } from "@/services/firmware";

export function SystemBuildInfo() {
  const healthQuery = useQuery({
    queryKey: ["appliance", "build"],
    queryFn: fetchApplianceBuildSnapshot,
    staleTime: 60_000,
    refetchInterval: 120_000,
  });
  const firmwareQuery = useQuery({
    queryKey: ["firmware"],
    queryFn: () => firmwareApi.info(),
    staleTime: 60_000,
    refetchInterval: 120_000,
  });

  const data = healthQuery.data;
  const staged = data?.staged;
  const running = firmwareQuery.data?.version || data?.runningFirmwareVersion || null;
  const stagedFirmware = staged?.firmwareVersion ?? null;
  const lastBuilt = formatBuildDate(staged?.adminBuild ?? staged?.stagedAt);
  const firmwareMismatch = Boolean(running && stagedFirmware && running !== stagedFirmware);
  const loading = healthQuery.isLoading && firmwareQuery.isLoading;
  const failed = healthQuery.isError && firmwareQuery.isError;

  return (
    <div className="space-y-3">
      <h4 className="text-[13px] font-semibold">Firmware & Build</h4>
      {loading ? (
        <div className="flex items-center gap-2 text-xs text-muted-foreground">
          <Loader2 className="h-3.5 w-3.5 animate-spin" />
          Loading build metadata…
        </div>
      ) : failed ? (
        <div className="space-y-2">
          <div className="rounded-md border bg-muted/20 px-3">
            <InfoRow label="Running Firmware" value="—" mono />
          </div>
          <p className="text-xs text-destructive">
            Could not load firmware version from the appliance.
          </p>
        </div>
      ) : (
        <div className="space-y-2">
          <div className="rounded-md border bg-muted/20 px-3">
            <InfoRow label="Running Firmware" value={running ?? "—"} mono />
          </div>
          {!staged ? (
            <p className="text-xs text-muted-foreground">
              Staged Admin build info unavailable. Re-run{" "}
              <span className="font-mono">npm run build:esp32</span> and upload SPIFFS (Admin
              dashboard assets), separate from firmware .bin flash.
            </p>
          ) : (
            <>
              {firmwareMismatch ? (
                <Alert variant="default" className="py-2">
                  <AlertDescription className="text-xs">
                    Running firmware ({running}) differs from staged SPIFFS build ({stagedFirmware}
                    ). Re-flash firmware .bin or re-run deploy:esp32 for Admin assets.
                  </AlertDescription>
                </Alert>
              ) : null}
              <Collapsible>
                <CollapsibleTrigger className="flex h-10 w-full items-center justify-between rounded-md border bg-muted/20 px-3 text-[13px] font-medium">
                  Advanced build information
                  <span className="flex items-center gap-1 text-xs text-muted-foreground">
                    Show details
                    <ChevronDown className="h-3.5 w-3.5" />
                  </span>
                </CollapsibleTrigger>
                <CollapsibleContent className="pt-2">
                  <div className="rounded-md border bg-muted/20 px-3">
                    <InfoRow label="Staged Firmware" value={stagedFirmware ?? "—"} mono />
                    <InfoRow label="Git" value={staged.gitCommit ?? "—"} mono />
                    <InfoRow label="Build" value={formatBuildNumber(staged.buildNumber)} mono />
                    <InfoRow label="Portal" value={staged.portalRevision ?? "—"} mono />
                    <InfoRow
                      label="Storage Contract"
                      value={
                        staged.storageContractVersion != null
                          ? String(staged.storageContractVersion)
                          : "—"
                      }
                      mono
                    />
                    <InfoRow
                      label="HTTP Contract"
                      value={
                        staged.httpContractVersion != null
                          ? String(staged.httpContractVersion)
                          : "—"
                      }
                      mono
                    />
                    <InfoRow
                      label="Device Profile"
                      value={
                        staged.deviceProfileVersion != null
                          ? String(staged.deviceProfileVersion)
                          : "—"
                      }
                      mono
                    />
                    <InfoRow label="Last Built" value={lastBuilt} mono />
                  </div>
                </CollapsibleContent>
              </Collapsible>
            </>
          )}
        </div>
      )}
    </div>
  );
}
