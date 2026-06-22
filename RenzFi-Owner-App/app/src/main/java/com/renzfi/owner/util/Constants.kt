package com.renzfi.owner.util

object Constants {
    const val DEFAULT_ESP32_IP = "10.40.0.2"
    const val DEFAULT_DEVICE_NAME = "My Vendo"
    const val MDNS_HOSTNAME = "renzfi.local"
    const val HEALTH_PATH = "/api/health"
    const val DEVICE_INFO_PATH = "/api/device/info"
    const val ADMIN_PATH = "/admin"
    const val HEALTH_TIMEOUT_SECONDS = 5L
    const val STATUS_REFRESH_INTERVAL_MS = 30_000L
    const val DELETE_CONFIRM_TEXT = "CONFIRM"
}
