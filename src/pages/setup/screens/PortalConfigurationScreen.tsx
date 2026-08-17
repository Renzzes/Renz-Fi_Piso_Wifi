import { useCallback, useState } from "react";
import { zodResolver } from "@hookform/resolvers/zod";
import { useForm } from "react-hook-form";
import { z } from "zod";
import { Button } from "@/components/ui/button";
import {
  SetupForm,
  SetupFormField,
  SetupFormSection,
  SetupInput,
  SetupSelect,
  SetupCheckbox,
  SetupActions,
} from "@/components/setup/SetupForm";
import { SetupInfoBanner } from "@/components/setup/SetupInfoBanner";
import { SetupStatusCard } from "@/components/setup/SetupStatusCard";
import { wizardTheme } from "@/components/setup/WizardTheme";
import { nextScreenAfterState } from "@/components/setup/stepRouter";
import { useProvisioning } from "@/contexts/ProvisioningContext";
import type { SetupScreenProps } from "@/pages/setup/SetupScreenProps";
import {
  readApplianceConfigDraft,
  writeApplianceConfigDraft,
} from "@/pages/setup/applianceConfigDraft";
import { provisioningClient } from "@/services/provisioning/provisioningClient";
import { setupErrorMessage } from "@/pages/setup/setupErrorMessages";
import type { ConfigurePortalResponse } from "@/types/routerProvisioning";
import {
  DEFAULT_PORTAL_APPLIANCE_CONFIG,
  PORTAL_LANGUAGE_OPTIONS,
  PORTAL_THEME_OPTIONS,
  type PortalApplianceConfig,
} from "@/lib/applianceConfiguration";

const portalSchema = z.object({
  portalName: z.string().trim().min(1, "Portal name is required."),
  welcomeMessage: z.string(),
  footerText: z.string(),
  theme: z.string(),
  language: z.string(),
  enableVoucher: z.boolean(),
  enableCoin: z.boolean(),
  autoPlayMusic: z.boolean(),
  showPauseButton: z.boolean(),
  showTerminateButton: z.boolean(),
});

type PortalFormValues = z.infer<typeof portalSchema>;

function toFormValues(portal: PortalApplianceConfig): PortalFormValues {
  return { ...portal };
}

export function PortalConfigurationScreen(_props: SetupScreenProps) {
  const { applyWorkflowData, setCurrentScreen, setLoading, clearError, progress } =
    useProvisioning();
  const draft = readApplianceConfigDraft();
  const [verifyResult, setVerifyResult] = useState<ConfigurePortalResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const form = useForm<PortalFormValues>({
    resolver: zodResolver(portalSchema),
    defaultValues: toFormValues(draft.portal),
    mode: "onSubmit",
  });

  const onSubmit = form.handleSubmit(async (values) => {
    clearError();
    setErrorMessage(null);
    setVerifyResult(null);
    setLoading(true);

    const portal: PortalApplianceConfig = { ...values };

    try {
      writeApplianceConfigDraft({ portal });
      const data = await provisioningClient.configurePortal(portal);
      setVerifyResult(data);

      if (data.verified && data.ok !== false) {
        writeApplianceConfigDraft({
          portal,
          portalRevision: data.revision ?? null,
          portalVerified: true,
        });
        applyWorkflowData(data);
        setCurrentScreen(nextScreenAfterState(data.installation.state));
      }
    } catch (err) {
      setErrorMessage(setupErrorMessage(err, "Portal verification failed."));
    } finally {
      setLoading(false);
    }
  });

  const restoreDefaults = useCallback(() => {
    form.reset(toFormValues(DEFAULT_PORTAL_APPLIANCE_CONFIG));
  }, [form]);

  const themeOptions = PORTAL_THEME_OPTIONS.map((option) => ({
    value: option.value,
    label: option.label,
  }));
  const languageOptions = PORTAL_LANGUAGE_OPTIONS.map((option) => ({
    value: option.value,
    label: option.label,
  }));

  return (
    <SetupForm aria-label="Portal configuration" onSubmit={(event) => void onSubmit(event)}>
      <div>
        <h2 className={wizardTheme.typography.title}>Portal configuration</h2>
        <p className={wizardTheme.typography.description}>
          Configure captive portal behaviour. Banner and music uploads remain in the Admin
          Dashboard after setup.
        </p>
      </div>

      {progress?.message ? (
        <SetupInfoBanner variant="info" title="Applying configuration" description={progress.message} />
      ) : null}

      <SetupInfoBanner
        variant="info"
        title="Bundled branding"
        description="Default banner and background music are verified on save. Custom media can be uploaded from Captive Portal settings later."
      />

      <SetupFormSection title="Portal identity">
        <SetupFormField
          label="Portal name"
          htmlFor="portal-name"
          required
          error={form.formState.errors.portalName?.message}
        >
          <SetupInput id="portal-name" {...form.register("portalName")} />
        </SetupFormField>

        <SetupFormField label="Welcome message" htmlFor="portal-welcome">
          <SetupInput id="portal-welcome" {...form.register("welcomeMessage")} />
        </SetupFormField>

        <SetupFormField label="Footer text" htmlFor="portal-footer">
          <SetupInput id="portal-footer" {...form.register("footerText")} />
        </SetupFormField>

        <SetupFormField label="Theme" htmlFor="portal-theme">
          <SetupSelect
            id="portal-theme"
            value={form.watch("theme")}
            onValueChange={(value) => form.setValue("theme", value)}
            options={themeOptions}
          />
        </SetupFormField>

        <SetupFormField label="Language" htmlFor="portal-language">
          <SetupSelect
            id="portal-language"
            value={form.watch("language")}
            onValueChange={(value) => form.setValue("language", value)}
            options={languageOptions}
          />
        </SetupFormField>
      </SetupFormSection>

      <SetupFormSection title="Portal features">
        <SetupCheckbox
          id="enable-voucher"
          label="Enable voucher"
          description="Show voucher code entry on the captive portal."
          checked={form.watch("enableVoucher")}
          onCheckedChange={(checked) => form.setValue("enableVoucher", checked === true)}
        />
        <SetupCheckbox
          id="enable-coin"
          label="Enable coin"
          description="Show insert-coin flow on the captive portal."
          checked={form.watch("enableCoin")}
          onCheckedChange={(checked) => form.setValue("enableCoin", checked === true)}
        />
        <SetupCheckbox
          id="auto-play-music"
          label="Auto play music"
          description="Play background music during coin insert countdown."
          checked={form.watch("autoPlayMusic")}
          onCheckedChange={(checked) => form.setValue("autoPlayMusic", checked === true)}
        />
        <SetupCheckbox
          id="show-pause"
          label="Pause button"
          description="Allow guests to pause their session timer."
          checked={form.watch("showPauseButton")}
          onCheckedChange={(checked) => form.setValue("showPauseButton", checked === true)}
        />
        <SetupCheckbox
          id="show-terminate"
          label="Terminate button"
          description="Allow guests to end their session early."
          checked={form.watch("showTerminateButton")}
          onCheckedChange={(checked) => form.setValue("showTerminateButton", checked === true)}
        />
      </SetupFormSection>

      {errorMessage ? (
        <SetupInfoBanner variant="error" title="Verification failed" description={errorMessage} />
      ) : null}

      {verifyResult && !verifyResult.verified ? (
        <SetupStatusCard
          status="error"
          title="Portal not verified"
          description={verifyResult.error ?? "Portal branding verification failed"}
        />
      ) : null}

      {verifyResult?.verified ? (
        <SetupStatusCard
          status="success"
          title="Portal verified"
          description="Captive portal branding is ready."
          details={[
            verifyResult.revision != null ? `Revision: ${verifyResult.revision}` : null,
            `Banner: ${verifyResult.hasBanner ? "custom" : "default bundled"}`,
            `Music: ${verifyResult.hasMusic ? "custom" : "default bundled"}`,
          ]
            .filter(Boolean)
            .join(" · ")}
        />
      ) : null}

      <SetupActions>
        <Button type="button" variant="outline" onClick={restoreDefaults}>
          Restore defaults
        </Button>
        <Button type="submit">Verify portal</Button>
      </SetupActions>
    </SetupForm>
  );
}
