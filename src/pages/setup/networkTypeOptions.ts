/**
 * Network mode constants for the installer UI.
 *
 * PRODUCT STANDARDIZATION (Phase 7D):
 * The official Renz-Fi appliance ships with a Renz-Fi Gateway (MikroTik RouterOS).
 * Installers no longer choose between network types — setup proceeds directly
 * into gateway configuration. Specific hardware models (e.g. hAP lite) are not
 * hardcoded in UI copy so future SKUs can ship without software redesign.
 *
 * GenericAPDriver / "standard" mode is RESERVED for future product editions.
 * It remains compiled and registered in RouterPlatform but is no longer
 * presented in the installer UI or pre-selected by any runtime path.
 *
 * Internal driverIds, RouterPlatform, IRouterDriver, and ProvisioningEngine
 * are unchanged. Only the installer copy and default selection changed.
 */

export type NetworkMode = "standard" | "mikrotik";

export const DRIVER_ID: Record<NetworkMode, string> = {
  /** @deprecated RESERVED — GenericAPDriver. Not used in standard installer flow. */
  standard: "generic_ap",
  mikrotik: "mikrotik",
};

/**
 * Returns the network mode for a stored driverId.
 * Defaults to "mikrotik" (official gateway) if unrecognised.
 */
export function networkModeFromDriverId(driverId: string | null): NetworkMode {
  if (driverId === "generic_ap") return "standard";
  return "mikrotik";
}

/**
 * RESERVED — kept for backward compatibility and potential future product editions.
 * Not presented in the official installer UI.
 */
export const RESERVED_STANDARD_NETWORK_OPTION = {
  mode: "standard" as NetworkMode,
  label: "Standard Network",
  deprecated: true,
  reserved: true,
  shortDescription: "Reserved for future product editions",
  longDescription:
    "GenericAPDriver — passive gateway mode. Not used in the standard Renz-Fi installer flow.",
  features: [
    "Captive portal",
    "Coin system",
    "Vouchers & promos",
    "Sales reports",
    "Multi-appliance management",
  ],
  connectionNote: "Enter your gateway IP to test the connection.",
} as const;

/** Official installer option — Renz-Fi Gateway (MikroTik RouterOS). */
export const OFFICIAL_GATEWAY_OPTION = {
  mode: "mikrotik" as NetworkMode,
  label: "Renz-Fi Gateway",
  subtitle: "Powered by MikroTik RouterOS",
  recommended: true,
  shortDescription: "Official supported gateway",
  longDescription:
    "The Renz-Fi Gateway is the required router for all official Renz-Fi appliances.",
  features: [
    "Hotspot integration",
    "Captive portal enforcement",
    "Voucher & bandwidth control",
    "User synchronization",
    "Gateway diagnostics",
  ],
  connectionNote: "You will need your RouterOS admin username and password.",
} as const;

/** All options including reserved ones — used by NETWORK_MODE_OPTIONS for backward compat. */
export const NETWORK_MODE_OPTIONS = [
  OFFICIAL_GATEWAY_OPTION,
  RESERVED_STANDARD_NETWORK_OPTION,
] as const;

export type NetworkModeOption = (typeof NETWORK_MODE_OPTIONS)[number];
