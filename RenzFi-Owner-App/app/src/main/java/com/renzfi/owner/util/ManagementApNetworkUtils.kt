package com.renzfi.owner.util

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager

/**
 * Detects whether the phone is on a Renz-Fi Management AP network.
 * SSID is used when available as a hint only; health probe at
 * [Constants.MANAGEMENT_AP_IP] is always authoritative before any action is taken.
 */
object ManagementApNetworkUtils {
    /**
     * Exact-match check against the fixed factory-setup SSID (Phase 8: every
     * appliance broadcasts the identical "Renz-Fi Setup" name, no per-unit
     * suffix). This is a hint only — actual setup availability is always
     * confirmed by probing [Constants.MANAGEMENT_AP_IP] before acting on it.
     */
    fun isSetupSsid(ssid: String?): Boolean {
        val normalized = ssid?.trim()?.removeSurrounding("\"")?.takeIf { it.isNotBlank() }
            ?: return false
        if (normalized.equals("<unknown ssid>", ignoreCase = true)) return false
        if (normalized == "0x") return false
        return normalized.equals(Constants.MANAGEMENT_AP_SSID, ignoreCase = true)
    }

    fun currentWifiSsid(context: Context): String? {
        val appContext = context.applicationContext
        if (!isOnWifi(appContext)) return null

        val wifiManager = appContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            ?: return null

        @Suppress("DEPRECATION")
        val ssid = wifiManager.connectionInfo?.ssid ?: return null
        val normalized = ssid.trim().removeSurrounding("\"")
        if (normalized.isBlank() ||
            normalized.equals("<unknown ssid>", ignoreCase = true) ||
            normalized == "0x"
        ) {
            return null
        }
        return normalized
    }

    fun isOnWifi(context: Context): Boolean {
        val connectivityManager =
            context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val network = connectivityManager.activeNetwork ?: return false
        val capabilities = connectivityManager.getNetworkCapabilities(network) ?: return false
        return capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
    }

    /** Human-readable hint for the setup SSID: the fixed "Renz-Fi Setup" name. */
    fun setupSsidHint(deviceId: String? = null): String = Constants.MANAGEMENT_AP_SSID
}
