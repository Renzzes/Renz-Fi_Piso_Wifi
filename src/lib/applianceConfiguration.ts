/**
 * Canonical appliance configuration — shared by Setup Wizard and Admin Dashboard.
 * Maps to CoinManager fields and portal behaviour (portal.json future / provisioning body).
 *
 * Do not duplicate these shapes in screen components.
 */

export type PortalApplianceConfig = {
  portalName: string;
  welcomeMessage: string;
  footerText: string;
  theme: string;
  language: string;
  enableVoucher: boolean;
  enableCoin: boolean;
  autoPlayMusic: boolean;
  showPauseButton: boolean;
  showTerminateButton: boolean;
};

export type CoinApplianceConfig = {
  enabled: boolean;
  pulsesPerPeso: number;
  debounceMs: number;
  settleMs: number;
  timeoutSeconds: number;
  defaultMinutesPerPeso: number;
  /** Display label — promo rates are managed in Admin after setup */
  pricingProfile: string;
};

export const PORTAL_THEME_OPTIONS = [
  { value: "default", label: "Default (Renz-Fi blue)" },
  { value: "dark", label: "Dark" },
  { value: "light", label: "Light" },
] as const;

export const PORTAL_LANGUAGE_OPTIONS = [
  { value: "en", label: "English" },
  { value: "fil", label: "Filipino" },
] as const;

/** Bundled captive portal defaults — matches login.html copy. */
export const DEFAULT_PORTAL_APPLIANCE_CONFIG: PortalApplianceConfig = {
  portalName: "Renz-Fi Piso WiFi",
  welcomeMessage: "Welcome! Insert a coin or enter a voucher to connect.",
  footerText: "Thank you for using our service!",
  theme: "default",
  language: "en",
  enableVoucher: true,
  enableCoin: true,
  autoPlayMusic: true,
  showPauseButton: true,
  showTerminateButton: true,
};

/** Mirrors RenzFiConfig / CoinManager factory defaults. */
export const RECOMMENDED_COIN_DEFAULTS: CoinApplianceConfig = {
  enabled: true,
  pulsesPerPeso: 1,
  debounceMs: 35,
  settleMs: 450,
  timeoutSeconds: 60,
  defaultMinutesPerPeso: 5,
  pricingProfile: "Default promo rates",
};

export function portalToProvisioningBody(portal: PortalApplianceConfig) {
  return { portal };
}

export function coinToProvisioningBody(coin: CoinApplianceConfig) {
  return {
    coin: {
      enabled: coin.enabled,
      pulsesPerPeso: coin.pulsesPerPeso,
      debounceMs: coin.debounceMs,
      settleMs: coin.settleMs,
      timeoutSeconds: coin.timeoutSeconds,
      defaultMinutesPerPeso: coin.defaultMinutesPerPeso,
    },
  };
}

/** Same payload shape as Admin Dashboard PUT /api/coin/settings. */
export function coinToAdminSettings(coin: CoinApplianceConfig): Record<string, string> {
  return {
    enabled: coin.enabled ? "true" : "false",
    pulse_width_ms: String(coin.debounceMs),
    calibration: String(coin.pulsesPerPeso),
    timeout_seconds: String(coin.timeoutSeconds),
    pulsesPerPeso: String(coin.pulsesPerPeso),
    debounceMs: String(coin.debounceMs),
    settleMs: String(coin.settleMs),
    defaultMinutesPerPeso: String(coin.defaultMinutesPerPeso),
  };
}

/** Parse Admin Dashboard GET /api/coin/settings into canonical model. */
export function coinFromAdminSettings(raw: Record<string, string>): CoinApplianceConfig {
  return {
    enabled: raw.enabled !== "false",
    pulsesPerPeso: Math.max(1, Number(raw.calibration ?? raw.pulsesPerPeso ?? 1) || 1),
    debounceMs: Number(raw.pulse_width_ms ?? raw.debounceMs ?? RECOMMENDED_COIN_DEFAULTS.debounceMs),
    settleMs: Number(raw.settleMs ?? RECOMMENDED_COIN_DEFAULTS.settleMs),
    timeoutSeconds: Number(
      raw.timeout_seconds ?? raw.timeoutSeconds ?? RECOMMENDED_COIN_DEFAULTS.timeoutSeconds,
    ),
    defaultMinutesPerPeso: Number(
      raw.defaultMinutesPerPeso ?? RECOMMENDED_COIN_DEFAULTS.defaultMinutesPerPeso,
    ),
    pricingProfile: RECOMMENDED_COIN_DEFAULTS.pricingProfile,
  };
}

export function mergePortalConfig(
  partial?: Partial<PortalApplianceConfig>,
): PortalApplianceConfig {
  return { ...DEFAULT_PORTAL_APPLIANCE_CONFIG, ...partial };
}

export function mergeCoinConfig(partial?: Partial<CoinApplianceConfig>): CoinApplianceConfig {
  return { ...RECOMMENDED_COIN_DEFAULTS, ...partial };
}
