import { Suspense, useCallback, useEffect, useRef } from "react";
import { useNavigate } from "react-router-dom";
import { toast } from "sonner";
import { WizardShell } from "@/components/setup/WizardShell";
import { ResumeGateModal } from "@/components/setup/ResumeGateModal";
import { SetupBootstrapScreen } from "@/components/setup/SetupCard";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { wizardClasses, wizardTheme } from "@/components/setup/WizardTheme";
import {
  canNavigateBack,
  canNavigateForward,
} from "@/components/setup/navigationGuards";
import {
  SETUP_SCREEN_ORDER,
  screenIndex,
  type SetupScreenId,
} from "@/components/setup/stepRouter";
import { SETUP_SCREEN_COMPONENTS } from "@/pages/setup/screens";
import { buildSetupScreenProps } from "@/pages/setup/SetupScreenProps";
import {
  persistSetupScreen,
  persistSetupScroll,
  readPersistedSetupScreen,
  resolveRestoredSetupScreen,
  restoreSetupScroll,
} from "@/pages/setup/setupSessionRecovery";
import { ProvisioningProvider, useProvisioning } from "@/contexts/ProvisioningContext";
import { useOnlineStatus } from "@/hooks/setup/useOnlineStatus";
import { useSetupMountOnce } from "@/hooks/setup/useSetupMountOnce";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";

function SetupWizardContent({ onExit }: { onExit: () => void }) {
  const {
    currentScreen,
    installation,
    workflowStep,
    loading,
    bootstrapping,
    error,
    sseConnected,
    setCurrentScreen,
    bootstrap,
    abortSetup,
    clearError,
  } = useProvisioning();
  const { online, offlineMessage } = useOnlineStatus();
  const restoredRef = useRef(false);

  // Mount once — never poll /api/provisioning/installation/resume.
  useSetupMountOnce(() => {
    void bootstrap();
  });

  useEffect(() => {
    if (bootstrapping || !installation) return;
    persistSetupScreen(currentScreen);
  }, [bootstrapping, currentScreen, installation]);

  useEffect(() => {
    if (bootstrapping || !installation || restoredRef.current) return;
    restoredRef.current = true;
    const persisted = readPersistedSetupScreen();
    const restored = resolveRestoredSetupScreen(persisted, installation.state);
    if (restored && restored !== currentScreen) {
      setCurrentScreen(restored);
    }
    restoreSetupScroll();
  }, [bootstrapping, currentScreen, installation, setCurrentScreen]);

  useEffect(() => {
    const onScroll = () => persistSetupScroll(window.scrollY);
    window.addEventListener("scroll", onScroll, { passive: true });
    return () => window.removeEventListener("scroll", onScroll);
  }, []);

  useEffect(() => {
    restoreSetupScroll();
  }, [currentScreen]);

  const installationState = installation?.state ?? "factory";

  const handleBack = useCallback(() => {
    clearError();
    const guard = canNavigateBack(currentScreen, installationState);
    if (!guard.allowed) {
      if (guard.reason) toast.message(guard.reason);
      return;
    }
    const idx = screenIndex(currentScreen);
    const prev = SETUP_SCREEN_ORDER[idx - 1];
    if (prev) setCurrentScreen(prev);
  }, [clearError, currentScreen, installationState, setCurrentScreen]);

  const handleNext = useCallback(async () => {
    clearError();
    const idx = screenIndex(currentScreen);
    const next = SETUP_SCREEN_ORDER[idx + 1];
    if (!next) return;

    const guard = canNavigateForward(currentScreen, next, installationState);
    if (!guard.allowed) {
      if (guard.reason) toast.message(guard.reason);
      return;
    }
    setCurrentScreen(next);
  }, [clearError, currentScreen, installationState, setCurrentScreen]);

  const handleCancel = useCallback(async () => {
    await abortSetup();
    toast.message("Setup paused. You can resume later from the admin menu.");
    onExit();
  }, [abortSetup, onExit]);

  const ScreenComponent = SETUP_SCREEN_COMPONENTS[currentScreen];
  const backGuard = canNavigateBack(currentScreen, installationState);
  const nextScreen = SETUP_SCREEN_ORDER[screenIndex(currentScreen) + 1];
  const forwardGuard = nextScreen
    ? canNavigateForward(currentScreen, nextScreen, installationState)
    : { allowed: false };

  if (bootstrapping) {
    return <SetupBootstrapScreen />;
  }

  if (error && !installation) {
    return (
      <div className={wizardClasses.errorPanel}>
        <p className="text-sm text-destructive text-center max-w-md" role="alert">
          {error}
        </p>
        <div className="flex flex-col sm:flex-row gap-2">
          <Button variant="outline" onClick={() => void bootstrap()}>
            Retry
          </Button>
          <Button variant="ghost" onClick={onExit}>
            Back to dashboard
          </Button>
        </div>
      </div>
    );
  }

  if (!installation || !workflowStep) {
    return <SetupBootstrapScreen message="Preparing setup wizard…" />;
  }

  const screenProps = buildSetupScreenProps({
    installation,
    workflowStep,
    loading,
    onNext: handleNext,
    onBack: handleBack,
  });

  const SELF_NAV_SCREENS: SetupScreenId[] = [
    "welcome",
    "router_detection",
    "driver_selection",
    "router_connection",
    "portal_configuration",
    "coin_configuration",
    "validation",
    "complete",
  ];
  const usesSelfNavigation = SELF_NAV_SCREENS.includes(currentScreen);

  return (
    <>
      <ResumeGateModal />
      <WizardShell
        currentScreen={currentScreen}
        loading={loading}
        error={error}
        canGoBack={!usesSelfNavigation && backGuard.allowed}
        canGoNext={!usesSelfNavigation && forwardGuard.allowed && online}
        onBack={handleBack}
        onNext={() => void handleNext()}
        onCancel={() => void handleCancel()}
        showNext={!usesSelfNavigation && currentScreen !== "complete"}
        nextLabel={currentScreen === "summary" ? "Finish setup" : "Next"}
      >
        {!online ? (
          <SetupInfoBanner variant="warning" title="Offline" description={offlineMessage} />
        ) : null}

        {!sseConnected && online ? (
          <SetupInfoBanner
            variant="info"
            title="Reconnecting live updates"
            description="Progress events will resume automatically when the connection is restored."
          />
        ) : null}

        <div
          key={currentScreen}
          className={cn(
            wizardTheme.motion.durationNormal,
            "motion-safe:opacity-100 motion-safe:transition-opacity",
          )}
        >
          <Suspense fallback={<SetupBootstrapScreen message="Loading step…" />}>
            <ScreenComponent {...screenProps} />
          </Suspense>
        </div>
      </WizardShell>
    </>
  );
}

export default function SetupWizardPage() {
  const navigate = useNavigate();

  const handleExit = useCallback(() => {
    navigate("/dashboard", { replace: true });
  }, [navigate]);

  return (
    <ProvisioningProvider enabled onReady={handleExit} onAborted={handleExit}>
      <SetupWizardContent onExit={handleExit} />
    </ProvisioningProvider>
  );
}

