import type { CoinApplianceConfig, PortalApplianceConfig } from "@/lib/applianceConfiguration";
import {
  DEFAULT_PORTAL_APPLIANCE_CONFIG,
  RECOMMENDED_COIN_DEFAULTS,
} from "@/lib/applianceConfiguration";

const STORAGE_KEY = "renz_setup_appliance_config";

export type ApplianceConfigDraft = {
  portal: PortalApplianceConfig;
  coin: CoinApplianceConfig;
  portalRevision: number | null;
  portalVerified: boolean;
  coinHardwareOk: boolean | null;
};

const defaultDraft = (): ApplianceConfigDraft => ({
  portal: { ...DEFAULT_PORTAL_APPLIANCE_CONFIG },
  coin: { ...RECOMMENDED_COIN_DEFAULTS },
  portalRevision: null,
  portalVerified: false,
  coinHardwareOk: null,
});

export function readApplianceConfigDraft(): ApplianceConfigDraft {
  try {
    const raw = sessionStorage.getItem(STORAGE_KEY);
    if (!raw) return defaultDraft();
    return { ...defaultDraft(), ...(JSON.parse(raw) as Partial<ApplianceConfigDraft>) };
  } catch {
    return defaultDraft();
  }
}

export function writeApplianceConfigDraft(
  partial: Partial<ApplianceConfigDraft>,
): ApplianceConfigDraft {
  const next = { ...readApplianceConfigDraft(), ...partial };
  sessionStorage.setItem(STORAGE_KEY, JSON.stringify(next));
  return next;
}

export function clearApplianceConfigDraft(): void {
  sessionStorage.removeItem(STORAGE_KEY);
}
