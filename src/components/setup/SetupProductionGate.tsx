import { useEffect, useState, type ReactNode } from "react";
import { Link } from "react-router-dom";
import { SetupBootstrapScreen } from "@/components/setup/SetupCard";
import { Button } from "@/components/ui/button";
import {
  fetchInstallationState,
  isProductionInstallationState,
} from "@/lib/installationState";

/**
 * After provisioning, the setup wizard must not be used — production Admin only.
 * Setup Unlock intentionally returns the appliance to a pre-ready state.
 *
 * Never fail-open into the wizard on health errors: /api/provisioning/* is not
 * registered on the production HTTP plane, so a false "allow" yields
 * "API endpoint not found".
 */
export function SetupProductionGate({ children }: { children: ReactNode }) {
  const [gate, setGate] = useState<
    "loading" | "allow" | "complete" | "unavailable"
  >("loading");
  const [detail, setDetail] = useState("");

  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const state = await fetchInstallationState();
        if (cancelled) return;
        if (isProductionInstallationState(state)) {
          setDetail(state ?? "provisioned");
          setGate("complete");
          return;
        }
        setGate("allow");
      } catch {
        if (!cancelled) {
          setDetail(
            "Could not verify installation state. Setup APIs are unavailable while the device is in production mode.",
          );
          setGate("unavailable");
        }
      }
    })();
    return () => {
      cancelled = true;
    };
  }, []);

  if (gate === "loading") {
    return <SetupBootstrapScreen message="Checking installation state…" />;
  }
  if (gate === "complete") {
    return (
      <div className="min-h-[50vh] flex flex-col items-center justify-center gap-3 p-6 text-center">
        <p className="text-sm font-medium">Setup is already complete</p>
        <p className="text-sm text-muted-foreground max-w-md">
          This appliance is installed ({detail}). Use System Settings → Setup
          Unlock if you need to reconfigure, or open the Management AP setup
          plane during installation.
        </p>
        <Button asChild>
          <Link to="/dashboard">Back to dashboard</Link>
        </Button>
      </div>
    );
  }
  if (gate === "unavailable") {
    return (
      <div className="min-h-[50vh] flex flex-col items-center justify-center gap-3 p-6 text-center">
        <p className="text-sm font-medium">Setup unavailable</p>
        <p className="text-sm text-muted-foreground max-w-md" role="alert">
          {detail ||
            "Setup wizard cannot run on this connection. The production plane does not expose /api/provisioning/*."}
        </p>
        <Button asChild variant="outline">
          <Link to="/dashboard">Back to dashboard</Link>
        </Button>
      </div>
    );
  }
  return <>{children}</>;
}
