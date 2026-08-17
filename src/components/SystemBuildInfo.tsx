import { useQuery } from "@tanstack/react-query";
import { Loader2 } from "lucide-react";
import { ConfigSection } from "@/components/ConfigSection";
import { Alert, AlertDescription } from "@/components/ui/alert";
import {
  formatBuildDate,
  formatBuildNumber,
} from "@/lib/buildMetadataDisplay";
import { fetchApplianceBuildSnapshot } from "@/services/applianceBuild";

function BuildRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-baseline justify-between gap-4 py-1.5 border-b border-border/50 last:border-0">
      <span className="text-xs text-muted-foreground">{label}</span>
      <span className="text-xs font-mono tabular-nums text-right">{value}</span>
    </div>
  );
}

export function SystemBuildInfo() {
  const { data, isLoading, isError } = useQuery({
    queryKey: ["appliance", "build"],
    queryFn: fetchApplianceBuildSnapshot,
    staleTime: 60_000,
    refetchInterval: 120_000,
  });

  const staged = data?.staged;
  const running = data?.runningFirmwareVersion;
  const firmwareDisplay =
    staged?.firmwareVersion ?? running ?? "—";
  const lastBuilt = formatBuildDate(staged?.adminBuild ?? staged?.stagedAt);
  const firmwareMismatch =
    Boolean(
      running &&
        staged?.firmwareVersion &&
        running !== staged.firmwareVersion,
    );

  return (
    <ConfigSection
      title="System"
      description="Staged build on SPIFFS — from the last npm run build:esp32 / deploy:esp32"
    >
      {isLoading ? (
        <div className="flex items-center gap-2 text-xs text-muted-foreground">
          <Loader2 className="h-3.5 w-3.5 animate-spin" />
          Loading build metadata…
        </div>
      ) : isError ? (
        <p className="text-xs text-destructive">
          Could not load build metadata from the appliance.
        </p>
      ) : !staged ? (
        <div className="space-y-2">
          <BuildRow label="Firmware" value={running ?? "—"} />
          <p className="text-xs text-muted-foreground">
            Staged build info unavailable. Re-run{" "}
            <span className="font-mono">npm run build:esp32</span> and upload SPIFFS.
          </p>
        </div>
      ) : (
        <div className="space-y-2">
          {firmwareMismatch ? (
            <Alert variant="default" className="py-2">
              <AlertDescription className="text-xs">
                Running firmware ({running}) differs from staged SPIFFS build (
                {staged.firmwareVersion}). Re-flash firmware or re-run deploy:esp32.
              </AlertDescription>
            </Alert>
          ) : null}
          <div className="rounded-md border bg-muted/20 px-3 py-1">
            <BuildRow label="Firmware" value={firmwareDisplay} />
            <BuildRow label="Git" value={staged.gitCommit ?? "—"} />
            <BuildRow label="Build" value={formatBuildNumber(staged.buildNumber)} />
            <BuildRow label="Portal" value={staged.portalRevision ?? "—"} />
            <BuildRow
              label="Storage Contract"
              value={
                staged.storageContractVersion != null
                  ? String(staged.storageContractVersion)
                  : "—"
              }
            />
            <BuildRow
              label="HTTP Contract"
              value={
                staged.httpContractVersion != null
                  ? String(staged.httpContractVersion)
                  : "—"
              }
            />
            <BuildRow
              label="Device Profile"
              value={
                staged.deviceProfileVersion != null
                  ? String(staged.deviceProfileVersion)
                  : "—"
              }
            />
            <BuildRow label="Last Built" value={lastBuilt} />
          </div>
        </div>
      )}
    </ConfigSection>
  );
}
