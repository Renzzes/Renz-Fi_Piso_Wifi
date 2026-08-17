import { useCallback, useEffect, useState } from "react";
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
import { readRouterDraft, writeRouterDraft } from "@/pages/setup/routerDraft";
import { networkModeFromDriverId } from "@/pages/setup/networkTypeOptions";
import { RENZFI_GATEWAY } from "@/pages/setup/productBranding";
import { provisioningClient } from "@/services/provisioning/provisioningClient";
import { setupErrorMessage } from "@/pages/setup/setupErrorMessages";
import { useFocusRestore } from "@/hooks/setup/useFocusRestore";
import { useSetupSubmitGuard } from "@/hooks/setup/useSetupSubmitGuard";
import type { ConnectRouterResponse } from "@/types/routerProvisioning";

// ── Standard Network form ─────────────────────────────────────────────────────

const standardSchema = z.object({
  host: z.string().trim().min(1, "Gateway IP is required."),
  rememberCredentials: z.boolean(),
});

type StandardFormValues = z.infer<typeof standardSchema>;

function StandardConnectionForm({
  draft,
  onSuccess,
}: {
  draft: ReturnType<typeof readRouterDraft>;
  onSuccess: (data: ConnectRouterResponse) => void;
}) {
  const { applyWorkflowData, setCurrentScreen, setLoading, clearError, progress } =
    useProvisioning();
  const { isSubmitting, runExclusive } = useSetupSubmitGuard();
  const { captureFocus, restoreFocus } = useFocusRestore();
  const [connectResult, setConnectResult] = useState<ConnectRouterResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const form = useForm<StandardFormValues>({
    resolver: zodResolver(standardSchema),
    defaultValues: {
      host: draft.host || "10.40.0.1",
      rememberCredentials: draft.rememberCredentials,
    },
    mode: "onSubmit",
  });

  const onSubmit = form.handleSubmit(async (values) => {
    captureFocus();
    await runExclusive(async () => {
      clearError();
      setErrorMessage(null);
      setConnectResult(null);
      setLoading(true);

      if (values.rememberCredentials) {
        writeRouterDraft({ host: values.host, rememberCredentials: true });
      }

      try {
        const data = await provisioningClient.connectRouter({
          host: values.host.trim(),
          // GenericAP does not require credentials — send empty strings.
          username: "",
          password: "",
        });
        setConnectResult(data);
        onSuccess(data);
        if (data.connected && data.ok !== false) {
          applyWorkflowData(data);
          setCurrentScreen(nextScreenAfterState(data.installation.state));
        }
      } catch (err) {
        setErrorMessage(setupErrorMessage(err, "Connection test failed."));
      } finally {
        setLoading(false);
        restoreFocus();
      }
    });
  });

  return (
    <SetupForm aria-label="Standard network connection" onSubmit={(e) => void onSubmit(e)}>
      <div>
        <h2 className={wizardTheme.typography.title}>Connect to your network</h2>
        <p className={wizardTheme.typography.description}>
          Enter the IP address of your gateway router or access point.
          This is usually printed on the device label.
        </p>
      </div>

      {progress?.message ? (
        <SetupInfoBanner variant="info" title="Testing connection" description={progress.message} />
      ) : null}

      <SetupFormSection title="Network settings">
        <SetupFormField
          label="Gateway IP address"
          htmlFor="router-host"
          required
          hint="The LAN IP of your router or access point — usually 192.168.1.1 or 10.40.0.1."
          error={form.formState.errors.host?.message}
        >
          <SetupInput
            id="router-host"
            autoComplete="off"
            placeholder="10.40.0.1"
            {...form.register("host")}
          />
        </SetupFormField>

        <SetupCheckbox
          id="remember-credentials"
          label="Remember this address"
          description="Store the IP for this setup session."
          checked={form.watch("rememberCredentials")}
          onCheckedChange={(checked) =>
            form.setValue("rememberCredentials", checked === true)
          }
        />
      </SetupFormSection>

      {errorMessage ? (
        <SetupInfoBanner variant="error" title="Connection failed" description={errorMessage} />
      ) : null}

      {connectResult ? (
        <SetupStatusCard
          status={connectResult.connected ? "success" : "error"}
          title={connectResult.connected ? "Network reachable" : "Connection failed"}
          description={connectResult.connected
            ? "The appliance can reach the network."
            : connectResult.error ?? "Could not reach the gateway."}
        />
      ) : null}

      <SetupActions>
        <Button type="submit" disabled={isSubmitting}>
          Test connection
        </Button>
      </SetupActions>
    </SetupForm>
  );
}

// ── MikroTik Enhanced form ────────────────────────────────────────────────────

const mikrotikSchema = z.object({
  host: z.string().trim().min(1, "Router IP is required."),
  username: z.string().trim().min(1, "Username is required."),
  password: z.string().min(1, "Password is required."),
  profile: z.string().optional(),
  rememberCredentials: z.boolean(),
});

type MikroTikFormValues = z.infer<typeof mikrotikSchema>;

function MikroTikConnectionForm({
  draft,
  onSuccess,
}: {
  draft: ReturnType<typeof readRouterDraft>;
  onSuccess: (data: ConnectRouterResponse) => void;
}) {
  const { applyWorkflowData, setCurrentScreen, setLoading, clearError, progress } =
    useProvisioning();
  const { isSubmitting, runExclusive } = useSetupSubmitGuard();
  const { captureFocus, restoreFocus } = useFocusRestore();
  const [profiles, setProfiles] = useState<string[]>([]);
  const [profilesError, setProfilesError] = useState<string | null>(null);
  const [connectResult, setConnectResult] = useState<ConnectRouterResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const form = useForm<MikroTikFormValues>({
    resolver: zodResolver(mikrotikSchema),
    defaultValues: {
      host: draft.host || "10.40.0.1",
      username: draft.username || "admin",
      password: "",
      profile: draft.profile || "",
      rememberCredentials: draft.rememberCredentials,
    },
    mode: "onSubmit",
  });

  const loadProfiles = useCallback(async () => {
    try {
      const data = await provisioningClient.listRouterProfiles();
      setProfiles(data.profiles ?? []);
      setProfilesError(null);
    } catch (err) {
      setProfiles([]);
      setProfilesError(setupErrorMessage(err, "Unable to load hotspot profiles."));
    }
  }, []);

  useEffect(() => {
    void loadProfiles();
  }, [loadProfiles]);

  const onSubmit = form.handleSubmit(async (values) => {
    captureFocus();
    await runExclusive(async () => {
      clearError();
      setErrorMessage(null);
      setConnectResult(null);
      setLoading(true);

      if (values.rememberCredentials) {
        writeRouterDraft({
          host: values.host,
          username: values.username,
          profile: values.profile ?? "",
          rememberCredentials: true,
        });
      }

      try {
        const data = await provisioningClient.connectRouter({
          host: values.host.trim(),
          username: values.username.trim(),
          password: values.password,
          profile: values.profile?.trim() || undefined,
        });
        setConnectResult(data);
        onSuccess(data);
        if (data.connected && data.ok !== false) {
          form.setValue("password", "");
          applyWorkflowData(data);
          setCurrentScreen(nextScreenAfterState(data.installation.state));
        }
      } catch (err) {
        setErrorMessage(setupErrorMessage(err, "Router connection test failed."));
      } finally {
        setLoading(false);
        restoreFocus();
      }
    });
  });

  const profileOptions = profiles.map((name) => ({ value: name, label: name }));

  return (
    <SetupForm aria-label="MikroTik router connection" onSubmit={(e) => void onSubmit(e)}>
      <div>
        <h2 className={wizardTheme.typography.title}>{RENZFI_GATEWAY.connectTitle}</h2>
        <p className={wizardTheme.typography.description}>
          {RENZFI_GATEWAY.connectDescription}
        </p>
      </div>

      {progress?.message ? (
        <SetupInfoBanner
          variant="info"
          title="Testing connection"
          description={progress.message}
        />
      ) : null}

      <SetupFormSection title={RENZFI_GATEWAY.credentialsSectionTitle}>
        <SetupFormField
          label="Router IP address"
          htmlFor="router-host"
          required
          error={form.formState.errors.host?.message}
        >
          <SetupInput
            id="router-host"
            autoComplete="off"
            placeholder="10.40.0.1"
            {...form.register("host")}
          />
        </SetupFormField>

        <SetupFormField
          label="Username"
          htmlFor="router-username"
          required
          error={form.formState.errors.username?.message}
        >
          <SetupInput
            id="router-username"
            autoComplete="username"
            {...form.register("username")}
          />
        </SetupFormField>

        <SetupFormField
          label="Password"
          htmlFor="router-password"
          required
          error={form.formState.errors.password?.message}
        >
          <SetupInput
            id="router-password"
            type="password"
            autoComplete="current-password"
            {...form.register("password")}
          />
        </SetupFormField>

        {profileOptions.length > 0 ? (
          <SetupFormField label="Hotspot profile" htmlFor="router-profile">
            <SetupSelect
              id="router-profile"
              value={form.watch("profile") || undefined}
              onValueChange={(value) => form.setValue("profile", value)}
              options={profileOptions}
              placeholder="Select hotspot profile"
            />
          </SetupFormField>
        ) : (
          <SetupFormField
            label="Hotspot profile"
            htmlFor="router-profile-manual"
            hint={profilesError ?? "Enter the hotspot profile name used on this router."}
          >
            <SetupInput id="router-profile-manual" {...form.register("profile")} />
          </SetupFormField>
        )}

        <SetupCheckbox
          id="remember-credentials"
          label="Remember credentials"
          description="Store host and username locally for this setup session."
          checked={form.watch("rememberCredentials")}
          onCheckedChange={(checked) =>
            form.setValue("rememberCredentials", checked === true)
          }
        />
      </SetupFormSection>

      {errorMessage ? (
        <SetupInfoBanner variant="error" title="Connection failed" description={errorMessage} />
      ) : null}

      {connectResult ? (
        <SetupStatusCard
          status={connectResult.connected ? "success" : "error"}
          title={connectResult.connected ? "Gateway connected" : "Connection failed"}
          description={
            connectResult.identity ??
            connectResult.error ??
            (connectResult.profileFound === false
              ? "Hotspot profile not found"
              : undefined)
          }
          details={
            connectResult.profileFound != null
              ? `Hotspot profile found: ${connectResult.profileFound ? "yes" : "no"}`
              : undefined
          }
        />
      ) : null}

      <SetupActions>
        <Button type="submit" disabled={isSubmitting}>
          Test connection
        </Button>
      </SetupActions>
    </SetupForm>
  );
}

// ── Branching wrapper ─────────────────────────────────────────────────────────

export function RouterConnectionScreen(props: SetupScreenProps) {
  const draft = readRouterDraft();
  const mode = networkModeFromDriverId(draft.selectedDriverId);
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  const [_result, setResult] = useState<ConnectRouterResponse | null>(null);

  if (mode === "mikrotik") {
    return <MikroTikConnectionForm draft={draft} onSuccess={setResult} />;
  }
  return <StandardConnectionForm draft={draft} onSuccess={setResult} />;
}
