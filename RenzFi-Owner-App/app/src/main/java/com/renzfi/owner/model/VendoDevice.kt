package com.renzfi.owner.model

import com.google.gson.annotations.SerializedName
import com.renzfi.owner.util.Constants
import java.util.UUID

/**
 * A registered Renz-Fi vendo installation.
 *
 * Remote access fields (MikroTik DDNS, public IP) are stored for future WireGuard VPN
 * integration. The ESP32 is never exposed directly to the internet.
 */
data class VendoDevice(
    @SerializedName("id") val id: String = UUID.randomUUID().toString(),
    @SerializedName("name") val name: String,
    @SerializedName("mikrotikDisplayName") val mikrotikDisplayName: String = "",
    @SerializedName("mikrotikDdns") val mikrotikDdns: String = "",
    @SerializedName("mikrotikPublicIp") val mikrotikPublicIp: String = "",
    @SerializedName("mikrotikNotes") val mikrotikNotes: String = "",
    @SerializedName("esp32LocalIp") val esp32LocalIp: String = Constants.DEFAULT_ESP32_IP,
    @SerializedName("lastSeen") val lastSeen: Long? = null,
    @SerializedName("isOnline") val isOnline: Boolean = false,
    @SerializedName("createdAt") val createdAt: Long = System.currentTimeMillis(),
)

enum class DeviceOnlineStatus {
    Online,
    Offline,
    Unknown,
}

fun VendoDevice.onlineStatus(): DeviceOnlineStatus = when {
    lastSeen == null -> DeviceOnlineStatus.Unknown
    isOnline -> DeviceOnlineStatus.Online
    else -> DeviceOnlineStatus.Offline
}
