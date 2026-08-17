/**
 * DriverSelectionScreen — silent passthrough (Phase 6C)
 *
 * The `driver_selection` workflow step still exists in the firmware state
 * machine. In the new installer flow the network type is chosen on the
 * NetworkTypeScreen (which calls selectDriver() and jumps straight to
 * router_connection), so this slot is bypassed during normal setup.
 *
 * If a user somehow lands here (e.g. deep-link, older resume token), the
 * screen transparently redirects them forward to router_connection so the
 * setup can continue without exposing any driver/manifest terminology.
 *
 * No firmware changes. No API changes. No context changes.
 */
import { useEffect } from "react";
import { SetupForm } from "@/components/setup/SetupForm";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { useProvisioning } from "@/contexts/ProvisioningContext";
import type { SetupScreenProps } from "@/pages/setup/SetupScreenProps";

export function DriverSelectionScreen(_props: SetupScreenProps) {
  const { setCurrentScreen } = useProvisioning();

  // Immediately forward to router_connection — this screen is not shown in
  // the Phase 6C installer flow.
  useEffect(() => {
    setCurrentScreen("router_connection");
  }, [setCurrentScreen]);

  // Render a minimal placeholder while the redirect fires.
  return (
    <SetupForm aria-label="Network type configured">
      <SetupInfoBanner
        variant="info"
        title="Continuing setup"
        description="Taking you to the next step…"
      />
    </SetupForm>
  );
}
