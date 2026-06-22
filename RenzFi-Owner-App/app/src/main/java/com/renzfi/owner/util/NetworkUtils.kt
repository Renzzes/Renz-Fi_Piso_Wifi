package com.renzfi.owner.util

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities

object NetworkUtils {
    fun isNetworkAvailable(context: Context): Boolean {
        val connectivityManager =
            context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val network = connectivityManager.activeNetwork ?: return false
        val capabilities = connectivityManager.getNetworkCapabilities(network) ?: return false
        return capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
            (
                capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
                    capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) ||
                    capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)
                )
    }

    fun buildBaseUrl(host: String): String = "http://${host.trim()}"

    fun buildHealthUrl(host: String): String = "${buildBaseUrl(host)}${Constants.HEALTH_PATH}"

    fun buildAdminUrl(host: String): String = "${buildBaseUrl(host)}${Constants.ADMIN_PATH}"

    fun isValidHost(host: String): Boolean {
        val trimmed = host.trim()
        if (trimmed.isEmpty()) return false
        val hostnamePattern = Regex(
            "^([a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)(\\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$",
        )
        val ipv4Pattern = Regex(
            "^(25[0-5]|2[0-4]\\d|1\\d{2}|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d{2}|[1-9]?\\d)){3}$",
        )
        return hostnamePattern.matches(trimmed) || ipv4Pattern.matches(trimmed)
    }
}
