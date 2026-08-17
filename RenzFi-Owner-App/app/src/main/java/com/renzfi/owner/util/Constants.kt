package com.renzfi.owner.util

object Constants {
    // ── LAN appliance ────────────────────────────────────────────────────────
    const val DEFAULT_ESP32_IP = "10.40.0.2"
    const val DEFAULT_DEVICE_NAME = "My Vendo"
    const val MDNS_HOSTNAME = "renzfi.local"
    const val HEALTH_PATH = "/api/health"
    const val DEVICE_INFO_PATH = "/api/device/info"
    const val ADMIN_PATH = "/admin"
    const val MANAGEMENT_AP_IP = "192.168.4.1"
    // Fixed, exact SSID (Phase 8) — no per-unit MAC/serial suffix. Every
    // Renz-Fi appliance broadcasts this exact name while unconfigured.
    const val MANAGEMENT_AP_SSID = "Renz-Fi Setup"
    @Deprecated(
        "Replaced by the exact MANAGEMENT_AP_SSID match (Phase 8 fixed SSID).",
        ReplaceWith("MANAGEMENT_AP_SSID"),
    )
    const val MANAGEMENT_AP_SSID_PREFIX = "RenzFi-Setup-"
    const val FACTORY_ADMIN_PASSWORD = "admin"
    const val HEALTH_TIMEOUT_SECONDS = 5L
    const val PROVISIONING_TIMEOUT_SECONDS = 30L
    // Bounded background probe used only for the single-saved-device cold-start
    // check. Must stay short so the app is never blocked on appliance
    // connectivity at launch — the outcome (online/offline) is informational,
    // not a navigation gate.
    const val STARTUP_PROBE_TIMEOUT_MS = 2_500L
    const val STATUS_REFRESH_INTERVAL_MS = 30_000L
    val FLEET_POLL_INTERVALS_MS = longArrayOf(15_000L, 30_000L, 60_000L)
    const val DEFAULT_FLEET_POLL_INTERVAL_MS = 30_000L
    const val DEFAULT_DISCOVERY_SUBNET = "192.168.88"
    const val DISCOVERY_CONCURRENCY = 16
    const val DELETE_CONFIRM_TEXT = "CONFIRM"

    // ── GitHub Releases (update system) ──────────────────────────────────────
    // TODO: set GITHUB_OWNER to your actual GitHub username before first release
    const val GITHUB_OWNER = "clareenz"
    const val GITHUB_REPO = "Renz-Fi_Piso_Wifi"
    const val GITHUB_TAG_PREFIX = "manager-android/v"
    const val UPDATE_CHECK_DELAY_MS = 7_000L
    const val UPDATE_APK_DIR = "updates"
    const val UPDATE_CHANNEL_STABLE = "stable"
    const val UPDATE_CHANNEL_BETA = "beta"
}
