package com.renzfi.owner.util

/**
 * Installer-facing product branding — Phase 7D.
 *
 * UI copy uses "Renz-Fi Gateway" instead of a specific MikroTik model so
 * future hardware SKUs do not require app redesign. Firmware still uses MikroTikDriver.
 */
object ProductBranding {
    const val GATEWAY_NAME = "Renz-Fi Gateway"
    const val GATEWAY_SUBTITLE = "Powered by MikroTik RouterOS"
    const val GATEWAY_BADGE = "Official gateway"

    const val GATEWAY_DESCRIPTION =
        "Provides hotspot enforcement, captive portal integration, voucher & bandwidth control, and user synchronization."

    const val GATEWAY_CONNECT_PROMPT =
        "Enter your Renz-Fi Gateway RouterOS admin credentials to enable hotspot integration."

    const val AP_REQUIREMENT =
        "Any access point that supports Bridge or Access Point mode can extend Wi-Fi coverage. " +
            "Routing, DHCP, hotspot, and enforcement stay on the Renz-Fi Gateway."

    val AP_EXAMPLES = listOf(
        "TP-Link",
        "COMFAST",
        "Ruijie",
        "Omada",
        "UniFi",
        "ASUS (AP mode)",
    )

    val AP_CONFIGURATION = listOf(
        "Access Point or Bridge mode (not router mode)",
        "DHCP disabled",
        "NAT disabled",
        "Firewall disabled",
        "Connected to Renz-Fi Gateway LAN",
    )
}
