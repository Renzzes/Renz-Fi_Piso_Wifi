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
import type { ConfigureCoinResponse } from "@/types/routerProvisioning";
import {
  RECOMMENDED_COIN_DEFAULTS,
  type CoinApplianceConfig,
} from "@/lib/applianceConfiguration";

const coinSchema = z.object({
  enabled: z.boolean(),
  pulsesPerPeso: z.coerce.number().int().min(1, "Pulse count must be at least 1."),
  timeoutSeconds: z.coerce
    .number()
    .int()
    .min(5, "Timeout must be at least 5 seconds.")
    .max(600, "Timeout must be 600 seconds or less."),
  settleMs: z.coerce
    .number()
    .int()
    .min(50, "Coin window must be at least 50 ms.")
    .max(5000, "Coin window must be 5000 ms or less."),
  defaultMinutesPerPeso: z.coerce
    .number()
    .int()
    .min(1, "Minutes per peso must be at least 1."),
});

type CoinFormValues = z.infer<typeof coinSchema>;

function toFormValues(coin: CoinApplianceConfig): CoinFormValues {
  return {
    enabled: coin.enabled,
    pulsesPerPeso: coin.pulsesPerPeso,
    timeoutSeconds: coin.timeoutSeconds,
    settleMs: coin.settleMs,
    defaultMinutesPerPeso: coin.defaultMinutesPerPeso,
  };
}

function toCoinConfig(values: CoinFormValues): CoinApplianceConfig {
  return {
    ...RECOMMENDED_COIN_DEFAULTS,
    ...values,
    pricingProfile: `₱1 = ${values.defaultMinutesPerPeso} min (default promo rates)`,
  };
}

function formatHardwareDetails(hardware?: Record<string, unknown>): string | undefined {
  if (!hardware) return undefined;
  const parts: string[] = [];
  if (hardware.enabled != null) parts.push(`Enabled: ${String(hardware.enabled)}`);
  if (hardware.fault != null) parts.push(`Fault: ${String(hardware.fault)}`);
  if (hardware.lastPulseMs != null) parts.push(`Last pulse: ${String(hardware.lastPulseMs)} ms`);
  if (hardware.pulseCount != null) parts.push(`Pulse count: ${String(hardware.pulseCount)}`);
  return parts.length > 0 ? parts.join(" · ") : JSON.stringify(hardware);
}

export function CoinConfigurationScreen(_props: SetupScreenProps) {
  const { applyWorkflowData, setCurrentScreen, setLoading, clearError, progress } =
    useProvisioning();
  const draft = readApplianceConfigDraft();
  const [configureResult, setConfigureResult] = useState<ConfigureCoinResponse | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);

  const form = useForm<CoinFormValues>({
    resolver: zodResolver(coinSchema),
    defaultValues: toFormValues(draft.coin),
    mode: "onSubmit",
  });

  const minutesPerPeso = form.watch("defaultMinutesPerPeso");
  const pricingProfile = `₱1 = ${minutesPerPeso || RECOMMENDED_COIN_DEFAULTS.defaultMinutesPerPeso} min (default promo rates)`;

  const restoreRecommendedDefaults = useCallback(() => {
    form.reset(toFormValues(RECOMMENDED_COIN_DEFAULTS));
    setConfigureResult(null);
    setErrorMessage(null);
  }, [form]);

  const onSubmit = form.handleSubmit(async (values) => {
    clearError();
    setErrorMessage(null);
    setConfigureResult(null);
    setLoading(true);

    const coin = toCoinConfig(values);

    try {
      writeApplianceConfigDraft({ coin });
      const data = await provisioningClient.configureCoin(coin);
      setConfigureResult(data);

      if (data.skipped || (data.hardwareOk && data.ok !== false)) {
        writeApplianceConfigDraft({
          coin,
          coinHardwareOk: data.skipped ? true : (data.hardwareOk ?? null),
        });
        applyWorkflowData(data);
        setCurrentScreen(nextScreenAfterState(data.installation.state));
        return;
      }

      if (data.ok === false) {
        setErrorMessage(data.error ?? "Coin hardware fault detected.");
      }
    } catch (err) {
      setErrorMessage(setupErrorMessage(err, "Coin configuration failed."));
    } finally {
      setLoading(false);
    }
  });

  return (
    <SetupForm aria-label="Coin configuration" onSubmit={(event) => void onSubmit(event)}>
      <div>
        <h2 className={wizardTheme.typography.title}>Coin configuration</h2>
        <p className={wizardTheme.typography.description}>
          Configure the coin acceptor and verify hardware readiness before validation.
        </p>
      </div>

      {progress?.message ? (
        <SetupInfoBanner variant="info" title="Configuring coin acceptor" description={progress.message} />
      ) : null}

      <SetupFormSection title="Coin acceptor">
        <SetupCheckbox
          id="coin-enabled"
          label="Coin enabled"
          description="Accept coin pulses for time credits."
          checked={form.watch("enabled")}
          onCheckedChange={(checked) => form.setValue("enabled", checked === true)}
        />

        <SetupFormField
          label="Pulse count"
          htmlFor="coin-pulses"
          hint="Pulses required per ₱1 (calibration)."
          required
          error={form.formState.errors.pulsesPerPeso?.message}
        >
          <SetupInput
            id="coin-pulses"
            type="number"
            min={1}
            {...form.register("pulsesPerPeso")}
          />
        </SetupFormField>

        <SetupFormField
          label="Coin timeout"
          htmlFor="coin-timeout"
          hint="Seconds to wait for coin insertion."
          required
          error={form.formState.errors.timeoutSeconds?.message}
        >
          <SetupInput
            id="coin-timeout"
            type="number"
            min={5}
            {...form.register("timeoutSeconds")}
          />
        </SetupFormField>

        <SetupFormField
          label="Coin window"
          htmlFor="coin-window"
          hint="Settling window in milliseconds between pulses."
          required
          error={form.formState.errors.settleMs?.message}
        >
          <SetupInput id="coin-window" type="number" min={50} {...form.register("settleMs")} />
        </SetupFormField>

        <SetupFormField
          label="Minutes per peso"
          htmlFor="coin-minutes"
          hint="Default time granted per ₱1 inserted."
          required
          error={form.formState.errors.defaultMinutesPerPeso?.message}
        >
          <SetupInput
            id="coin-minutes"
            type="number"
            min={1}
            {...form.register("defaultMinutesPerPeso")}
          />
        </SetupFormField>
      </SetupFormSection>

      <SetupFormSection title="Pricing profile">
        <SetupInfoBanner
          variant="info"
          title={pricingProfile}
          description="Promo packages and advanced rates can be managed from the Admin Dashboard after installation."
        />
      </SetupFormSection>

      {configureResult?.skipped ? (
        <SetupStatusCard
          status="warning"
          title="Coin step skipped"
          description={
            configureResult.reason ??
            "Coin hardware disabled in this firmware build — step skipped."
          }
        />
      ) : null}

      {errorMessage ? (
        <SetupInfoBanner variant="error" title="Configuration failed" description={errorMessage} />
      ) : null}

      {configureResult && !configureResult.skipped ? (
        <SetupStatusCard
          status={
            configureResult.hardwareOk
              ? "success"
              : configureResult.ok === false
                ? "error"
                : "pending"
          }
          title={
            configureResult.hardwareOk
              ? "Coin hardware ready"
              : configureResult.ok === false
                ? "Coin hardware fault"
                : "Coin diagnostics"
          }
          description={configureResult.error}
          details={formatHardwareDetails(configureResult.hardware)}
        />
      ) : null}

      {!configureResult && draft.coinHardwareOk != null ? (
        <SetupStatusCard
          status={draft.coinHardwareOk ? "success" : "error"}
          title={draft.coinHardwareOk ? "Previously verified" : "Hardware check failed"}
          description="Re-run configuration to refresh diagnostics."
        />
      ) : null}

      <SetupActions>
        <Button type="button" variant="outline" onClick={restoreRecommendedDefaults}>
          Restore recommended defaults
        </Button>
        <Button type="submit">Save and verify hardware</Button>
      </SetupActions>
    </SetupForm>
  );
}
