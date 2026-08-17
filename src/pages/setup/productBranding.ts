/**
 * Installer-facing product branding — Phase 7D.
 *
 * Use "Renz-Fi Gateway" in UI copy instead of a specific MikroTik model so
 * future hardware SKUs (hEX, hAP ac², ax lite, etc.) do not require
 * software or documentation redesign. Firmware still uses MikroTikDriver.
 */

export const RENZFI_GATEWAY = {
  name: "Renz-Fi Gateway",
  subtitle: "Powered by MikroTik RouterOS",
  badge: "Official gateway",
  description:
    "The Renz-Fi Gateway is the required router for all official Renz-Fi appliances. " +
    "It provides hotspot enforcement, captive portal integration, bandwidth control, " +
    "and user synchronization.",
  features: [
    "Hotspot integration",
    "Captive portal enforcement",
    "Voucher & bandwidth control",
    "User synchronization",
    "Gateway diagnostics",
  ],
  credentialsSectionTitle: "Renz-Fi Gateway credentials",
  connectTitle: "Connect to Renz-Fi Gateway",
  connectDescription:
    "Enter your RouterOS admin credentials to enable hotspot integration.",
} as const;

export const OPTIONAL_ACCESS_POINTS = {
  title: "Optional Wi-Fi coverage",
  requirement:
    "Any access point that supports Bridge or Access Point mode can extend Wi-Fi coverage. " +
    "The device must not perform routing, DHCP, hotspot, or enforcement — those remain on the Renz-Fi Gateway.",
  examplesLabel: "Examples:",
  examples: [
    "TP-Link",
    "COMFAST",
    "Ruijie",
    "Omada",
    "UniFi",
    "ASUS (AP mode)",
  ],
  configuration: [
    "Access Point or Bridge mode (not router mode)",
    "DHCP disabled",
    "NAT disabled",
    "Firewall disabled",
    "Connected to Renz-Fi Gateway LAN",
  ],
} as const;
