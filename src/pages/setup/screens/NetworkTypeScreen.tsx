import { useCallback, useEffect, useState } from "react";
import { Button } from "@/components/ui/button";
import { SetupForm, SetupActions } from "@/components/setup/SetupForm";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { nextScreenAfterState } from "@/components/setup/stepRouter";
import { cn } from "@/lib/utils";
import { useProvisioning } from "@/contexts/ProvisioningContext";
import type { SetupScreenProps } from "@/pages/setup/SetupScreenProps";
import { writeRouterDraft } from "@/pages/setup/routerDraft";
import { DRIVER_ID } from "@/pages/setup/networkTypeOptions";
import { OPTIONAL_ACCESS_POINTS, RENZFI_GATEWAY } from "@/pages/setup/productBranding";
import { provisioningClient } from "@/services/provisioning/provisioningClient";
import { setupErrorMessage } from "@/pages/setup/setupErrorMessages";
import { useSetupSubmitGuard } from "@/hooks/setup/useSetupSubmitGuard";
import { CheckCircle2, Router, Wifi } from "lucide-react";

/**
 * NetworkSetupScreen (formerly NetworkTypeScreen)
 *
 * Phase 7D: The official Renz-Fi appliance ships with a Renz-Fi Gateway
 * (MikroTik RouterOS). Installers no longer choose between network types —
 * this screen confirms the gateway and proceeds directly into RouterOS setup.
 *
 * GenericAPDriver remains compiled and registered in RouterPlatform.
 * It is hidden from the installer UI and marked reserved for future editions.
 * No firmware changes. No API changes. No ProvisioningEngine changes.
 */

function GatewayCard() {
  return (
    <div
      className={cn(
        "w-full rounded-xl border-2 border-primary bg-primary/5 p-4 md:p-5",
        "ring-1 ring-primary/20",
      )}
      role="status"
      aria-label="Supported gateway"
    >
      <div className="flex items-start justify-between gap-3 mb-3">
        <div className="space-y-0.5">
          <div className="flex flex-wrap items-center gap-2">
            <Router className="h-4 w-4 text-primary shrink-0" aria-hidden />
            <span className="text-base font-semibold leading-tight">
              {RENZFI_GATEWAY.name}
            </span>
            <span className="text-[11px] font-medium bg-primary/10 text-primary px-2 py-0.5 rounded-full">
              {RENZFI_GATEWAY.badge}
            </span>
          </div>
          <p className="text-xs text-muted-foreground pl-6">{RENZFI_GATEWAY.subtitle}</p>
        </div>
        <CheckCircle2 className="h-5 w-5 shrink-0 text-primary mt-0.5" aria-hidden />
      </div>

      <p className="text-sm text-muted-foreground mb-3 pl-6">
        {RENZFI_GATEWAY.description}
      </p>

      <ul className="space-y-1.5 pl-6" role="list">
        {RENZFI_GATEWAY.features.map((feature) => (
          <li key={feature} className="flex items-center gap-2 text-sm">
            <CheckCircle2 className="h-3.5 w-3.5 shrink-0 text-primary" aria-hidden />
            <span className="text-foreground">{feature}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}

function AccessPointsNote() {
  return (
    <div className="rounded-lg border border-border bg-muted/30 p-4 space-y-3">
      <div className="flex items-center gap-2">
        <Wifi className="h-4 w-4 text-muted-foreground shrink-0" aria-hidden />
        <span className="text-sm font-medium">{OPTIONAL_ACCESS_POINTS.title}</span>
        <span className="text-[11px] font-medium bg-muted text-muted-foreground px-2 py-0.5 rounded-full">
          Optional
        </span>
      </div>
      <p className="text-sm text-muted-foreground">
        {OPTIONAL_ACCESS_POINTS.requirement}
      </p>
      <p className="text-sm font-medium text-muted-foreground">
        {OPTIONAL_ACCESS_POINTS.examplesLabel}
      </p>
      <ul className="space-y-1 text-sm text-muted-foreground">
        {OPTIONAL_ACCESS_POINTS.examples.map((ap) => (
          <li key={ap} className="flex items-center gap-2">
            <span className="h-1.5 w-1.5 rounded-full bg-muted-foreground/60 shrink-0" aria-hidden />
            {ap}
          </li>
        ))}
      </ul>
      <div className="text-xs text-muted-foreground space-y-0.5 border-t border-border/50 pt-2">
        <p className="font-medium">Configure each access point as:</p>
        {OPTIONAL_ACCESS_POINTS.configuration.map((item) => (
          <p key={item}>✓ {item}</p>
        ))}
      </div>
    </div>
  );
}

export function NetworkTypeScreen(_props: SetupScreenProps) {
  const { applyWorkflowData, setCurrentScreen, setLoading, clearError, progress } =
    useProvisioning();
  const { isSubmitting, runExclusive } = useSetupSubmitGuard();
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  // Run detection silently — confirms MikroTik is reachable before connecting.
  const [mikrotikConfirmed, setMikrotikConfirmed] = useState(false);
  useEffect(() => {
    let cancelled = false;
    provisioningClient
      .detectRouters()
      .then((data) => {
        if (cancelled) return;
        const found =
          data.drivers?.some(
            (d) => d.driverId === "mikrotik" && (d.detected === true || d.configured === true),
          ) ?? false;
        if (found) setMikrotikConfirmed(true);
      })
      .catch(() => {
        // Non-fatal — installer continues regardless.
      });
    return () => {
      cancelled = true;
    };
  }, []);

  const handleContinue = useCallback(async () => {
    await runExclusive(async () => {
      clearError();
      setErrorMessage(null);
      setLoading(true);

      // MikroTik is the official driver — always selected on this screen.
      const driverId = DRIVER_ID["mikrotik"];

      writeRouterDraft({
        selectedDriverId: driverId,
        selectedManifest: null,
        host: "10.40.0.1",
        username: "admin",
      });

      try {
        const data = await provisioningClient.selectDriver({ driverId });
        applyWorkflowData(data);
        setCurrentScreen(nextScreenAfterState(data.installation.state));
      } catch (err) {
        setErrorMessage(setupErrorMessage(err, "Unable to configure gateway. Please try again."));
      } finally {
        setLoading(false);
      }
    });
  }, [applyWorkflowData, clearError, runExclusive, setCurrentScreen, setLoading]);

  return (
    <SetupForm aria-label="Network setup">
      <div>
        <div className="flex items-center gap-2 mb-1">
          <Router className="h-5 w-5 text-muted-foreground" aria-hidden />
          <h2 className={wizardTheme.typography.title}>Network setup</h2>
        </div>
        <p className={wizardTheme.typography.description}>
          The Renz-Fi v1 appliance works with the Renz-Fi Gateway as the
          required router. Optional access points extend Wi-Fi coverage.
        </p>
      </div>

      {mikrotikConfirmed ? (
        <SetupInfoBanner
          variant="success"
          title="Renz-Fi Gateway detected"
          description="A MikroTik RouterOS gateway was found on your network."
        />
      ) : null}

      {progress?.message ? (
        <SetupInfoBanner variant="info" title="Working" description={progress.message} />
      ) : null}

      <div>
        <p className="text-xs font-medium text-muted-foreground uppercase tracking-wide mb-2">
          Supported gateway
        </p>
        <GatewayCard />
      </div>

      <AccessPointsNote />

      {errorMessage ? (
        <SetupInfoBanner variant="error" title="Could not continue" description={errorMessage} />
      ) : null}

      <SetupActions>
        <Button type="button" disabled={isSubmitting} onClick={() => void handleContinue()}>
          Continue to gateway setup
        </Button>
      </SetupActions>
    </SetupForm>
  );
}
