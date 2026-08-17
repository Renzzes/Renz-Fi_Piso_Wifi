import { useCallback, useState } from "react";
import { useNavigate } from "react-router-dom";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { Button } from "@/components/ui/button";
import {
  SetupForm,
  SetupFormField,
  SetupFormSection,
  SetupInput,
} from "@/components/setup/SetupForm";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { resumeScreenForState } from "@/components/setup/stepRouter";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { useProvisioning } from "@/contexts/ProvisioningContext";
import type { SetupScreenProps } from "@/pages/setup/SetupScreenProps";
import { provisioningClient } from "@/services/provisioning/provisioningClient";
import { clearApplianceConfigDraft } from "@/pages/setup/applianceConfigDraft";
import { clearValidationDraft } from "@/pages/setup/validationDraft";
import { clearPersistedSetupSession } from "@/pages/setup/setupSessionRecovery";
import { setupErrorMessage } from "@/pages/setup/setupErrorMessages";
import { useFocusRestore } from "@/hooks/setup/useFocusRestore";
import { useOnlineStatus } from "@/hooks/setup/useOnlineStatus";
import { useSetupSubmitGuard } from "@/hooks/setup/useSetupSubmitGuard";

export function WelcomeScreen({ installation, session }: SetupScreenProps) {
  const navigate = useNavigate();
  const {
    applyWorkflowData,
    setCurrentScreen,
    continueResume,
    startOver,
    setLoading,
    clearError,
    resumePromptOpen,
  } = useProvisioning();
  const { online, offlineMessage } = useOnlineStatus();
  const { isSubmitting, runExclusive } = useSetupSubmitGuard();
  const { captureFocus, restoreFocus } = useFocusRestore();

  const [installerName, setInstallerName] = useState(session.installerName ?? "");
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [factoryResetOpen, setFactoryResetOpen] = useState(false);

  const canResume =
    Boolean(session.sessionId) &&
    installation.state !== "factory" &&
    !installation.ready;

  const handleBegin = useCallback(async () => {
    if (!online) {
      setErrorMessage(offlineMessage);
      return;
    }
    captureFocus();
    await runExclusive(async () => {
      clearError();
      setErrorMessage(null);
      setLoading(true);
      try {
        const data = await provisioningClient.begin({
          installerName: installerName.trim() || undefined,
        });
        if (data.alreadyReady || data.installation?.ready) {
          navigate("/dashboard", { replace: true });
          return;
        }
        applyWorkflowData(data);
        setCurrentScreen("router_detection");
      } catch (err) {
        setErrorMessage(setupErrorMessage(err, "Unable to start installation."));
      } finally {
        setLoading(false);
        restoreFocus();
      }
    });
  }, [
    applyWorkflowData,
    captureFocus,
    clearError,
    installerName,
    navigate,
    offlineMessage,
    online,
    restoreFocus,
    runExclusive,
    setCurrentScreen,
    setLoading,
  ]);

  const handleResume = useCallback(() => {
    if (resumePromptOpen) continueResume();
    setCurrentScreen(resumeScreenForState(installation.state));
  }, [continueResume, installation.state, resumePromptOpen, setCurrentScreen]);

  const handleFactoryReset = useCallback(async () => {
    setFactoryResetOpen(false);
    setErrorMessage(null);
    clearApplianceConfigDraft();
    clearValidationDraft();
    clearPersistedSetupSession();
    await startOver();
  }, [startOver]);

  return (
    <>
      <SetupForm aria-label="Welcome">
        <div>
          <h2 className={wizardTheme.typography.title}>Renz-Fi appliance setup</h2>
          <p className={wizardTheme.typography.description}>
            Configure your Renz-Fi Gateway, captive portal, and coin hardware for this Piso WiFi node.
          </p>
        </div>

        <SetupFormSection title="Appliance">
          <SetupFormField label="Appliance" htmlFor="appliance-name">
            <SetupInput
              id="appliance-name"
              readOnly
              value="Renz-Fi Piso WiFi"
              aria-readonly
            />
          </SetupFormField>
          <SetupFormField label="Firmware version" htmlFor="firmware-version">
            <SetupInput
              id="firmware-version"
              readOnly
              value={installation.firmwareVersion ?? "—"}
              aria-readonly
            />
          </SetupFormField>
          {session.sessionId ? (
            <SetupInfoBanner
              variant="info"
              title="Installation session"
              description={`Session ${session.sessionId}${session.elapsedMinutes != null ? ` · started ${session.elapsedMinutes} min ago` : ""}`}
            />
          ) : null}
        </SetupFormSection>

        <SetupFormSection title="Installer">
          <SetupFormField
            label="Installer name"
            htmlFor="installer-name"
            hint="Optional — stored with the installation session."
          >
            <SetupInput
              id="installer-name"
              value={installerName}
              onChange={(event) => setInstallerName(event.target.value)}
              placeholder="Field technician name"
              autoComplete="name"
            />
          </SetupFormField>
        </SetupFormSection>

        {!online ? (
          <SetupInfoBanner variant="warning" title="Offline" description={offlineMessage} />
        ) : null}

        {errorMessage ? (
          <SetupInfoBanner variant="error" title="Unable to start" description={errorMessage} />
        ) : null}

        <div className={wizardTheme.form.actions}>
          <Button
            type="button"
            disabled={isSubmitting || !online}
            onClick={() => void handleBegin()}
          >
            Begin installation
          </Button>
          {canResume ? (
            <Button type="button" variant="secondary" disabled={!online} onClick={handleResume}>
              Resume installation
            </Button>
          ) : null}
          <Button
            type="button"
            variant="outline"
            disabled={isSubmitting}
            onClick={() => setFactoryResetOpen(true)}
          >
            Factory reset
          </Button>
        </div>
      </SetupForm>

      <AlertDialog open={factoryResetOpen} onOpenChange={setFactoryResetOpen}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Reset setup progress?</AlertDialogTitle>
            <AlertDialogDescription>
              This will reset setup progress. Router settings and portal files are not erased.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>Cancel</AlertDialogCancel>
            <AlertDialogAction onClick={() => void handleFactoryReset()}>
              Start over
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </>
  );
}
