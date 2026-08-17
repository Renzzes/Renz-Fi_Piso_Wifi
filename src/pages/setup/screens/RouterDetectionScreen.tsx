import { useCallback, useState } from "react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { SetupEmptyState, SETUP_EMPTY_PRESETS } from "@/components/setup/SetupEmptyState";
import { SetupForm, SetupFormSection, SetupActions } from "@/components/setup/SetupForm";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { cn } from "@/lib/utils";
import { useProvisioning } from "@/contexts/ProvisioningContext";
import type { SetupScreenProps } from "@/pages/setup/SetupScreenProps";
import { writeRouterDraft } from "@/pages/setup/routerDraft";
import {
  capabilityLabels,
  formatConfidence,
  mergeDriverEntries,
  stabilityLabel,
} from "@/pages/setup/routerManifestUtils";
import { provisioningClient } from "@/services/provisioning/provisioningClient";
import { setupErrorMessage } from "@/pages/setup/setupErrorMessages";
import { useSetupMountOnce } from "@/hooks/setup/useSetupMountOnce";
import { useSetupSubmitGuard } from "@/hooks/setup/useSetupSubmitGuard";
import type { DetectRoutersResponse } from "@/types/routerProvisioning";
import { ExternalLink } from "lucide-react";

export function RouterDetectionScreen(_props: SetupScreenProps) {
  const { setCurrentScreen, setLoading, clearError, progress } = useProvisioning();
  const { isSubmitting, runExclusive } = useSetupSubmitGuard();
  const [detectData, setDetectData] = useState<DetectRoutersResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const runDetect = useCallback(async () => {
    await runExclusive(async () => {
      clearError();
      setErrorMessage(null);
      setLoading(true);
      try {
        const data = await provisioningClient.detectRouters();
        setDetectData(data);
        writeRouterDraft({ detectCache: data });
      } catch (err) {
        setErrorMessage(setupErrorMessage(err, "Unable to detect router drivers."));
        setDetectData(null);
      } finally {
        setLoading(false);
      }
    });
  }, [clearError, runExclusive, setLoading]);

  useSetupMountOnce(() => {
    void runDetect();
  });

  const entries = mergeDriverEntries(detectData?.available, detectData?.drivers);
  const hasDrivers = entries.length > 0;

  const handleSelect = (driverId: string) => {
    const entry = entries.find((item) => item.manifest.driverId === driverId);
    writeRouterDraft({
      selectedDriverId: driverId,
      selectedManifest: entry?.manifest ?? null,
      host: entry?.detection.host ?? "",
      firmware: entry?.manifest.supportedFirmware ?? "",
    });
    setCurrentScreen("driver_selection");
  };

  return (
    <SetupForm aria-label="Router detection">
      <div>
        <h2 className={wizardTheme.typography.title}>Router detection</h2>
        <p className={wizardTheme.typography.description}>
          Select the driver that matches your router hardware.
        </p>
      </div>

      <SetupInfoBanner
        variant="info"
        title="Recommended"
        description="Use RouterOS 7.18 or newer for MikroTik hotspot integration."
      />

      {progress?.message ? (
        <SetupInfoBanner variant="info" title="Scanning" description={progress.message} />
      ) : null}

      {errorMessage ? (
        <SetupInfoBanner variant="error" title="Detection failed" description={errorMessage} />
      ) : null}

      {!errorMessage && !hasDrivers ? (
        <SetupEmptyState
          icon={SETUP_EMPTY_PRESETS.noRouters.icon}
          title={SETUP_EMPTY_PRESETS.noRouters.title}
          description="Connect the router to the LAN port and ensure it is powered on."
        >
          <Button type="button" variant="outline" size="sm" disabled={isSubmitting} onClick={() => void runDetect()}>
            Retry
          </Button>
        </SetupEmptyState>
      ) : null}

      {hasDrivers ? (
        <SetupFormSection title="Detected drivers">
          <div className={wizardTheme.form.sectionGap}>
            {entries.map(({ manifest, detection }) => (
              <button
                key={manifest.driverId}
                type="button"
                onClick={() => handleSelect(manifest.driverId)}
                className={cn(
                  wizardTheme.form.section,
                  "w-full text-left transition-colors hover:bg-muted/40 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring",
                )}
              >
                <div className="flex flex-wrap items-start justify-between gap-2">
                  <div>
                    <p className={wizardTheme.form.sectionTitle}>{manifest.vendor}</p>
                    <p className={wizardTheme.form.sectionDescription}>
                      {manifest.model || manifest.driverId}
                    </p>
                  </div>
                  <Badge variant={manifest.stability === "experimental" ? "outline" : "secondary"}>
                    {stabilityLabel(manifest.stability)}
                  </Badge>
                </div>

                <dl className="grid gap-1.5 text-xs mt-3">
                  <div className="flex justify-between gap-3">
                    <dt className={wizardTheme.typography.meta}>Driver version</dt>
                    <dd>{manifest.driverVersion ?? "—"}</dd>
                  </div>
                  <div className="flex justify-between gap-3">
                    <dt className={wizardTheme.typography.meta}>Supported firmware</dt>
                    <dd>{manifest.supportedFirmware ?? "—"}</dd>
                  </div>
                  <div className="flex justify-between gap-3">
                    <dt className={wizardTheme.typography.meta}>Detection confidence</dt>
                    <dd>{formatConfidence(detection.confidence)}</dd>
                  </div>
                  {detection.reason ? (
                    <div className="flex justify-between gap-3">
                      <dt className={wizardTheme.typography.meta}>Detection note</dt>
                      <dd className="text-right max-w-[60%]">{detection.reason}</dd>
                    </div>
                  ) : null}
                </dl>

                {capabilityLabels(manifest.capabilities).length > 0 ? (
                  <div className="flex flex-wrap gap-1.5 mt-3">
                    {capabilityLabels(manifest.capabilities).map((label) => (
                      <Badge key={label} variant="outline" className="text-[10px]">
                        {label}
                      </Badge>
                    ))}
                  </div>
                ) : null}

                {manifest.documentationUrl ? (
                  <a
                    href={manifest.documentationUrl}
                    target="_blank"
                    rel="noreferrer"
                    className="inline-flex items-center gap-1 text-xs text-primary mt-3 hover:underline"
                    onClick={(event) => event.stopPropagation()}
                  >
                    Documentation
                    <ExternalLink className="h-3 w-3" />
                  </a>
                ) : null}
              </button>
            ))}
          </div>
        </SetupFormSection>
      ) : null}

      <SetupActions bordered={false}>
        <Button type="button" variant="outline" disabled={isSubmitting} onClick={() => void runDetect()}>
          Retry detection
        </Button>
      </SetupActions>
    </SetupForm>
  );
}
