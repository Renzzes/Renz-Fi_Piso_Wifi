package com.renzfi.owner.model

import com.google.gson.annotations.SerializedName

data class NetworkStatusResponse(
    @SerializedName("success") val success: Boolean = false,
    @SerializedName("data") val data: NetworkStatusData? = null,
)

data class NetworkStatusData(
    @SerializedName("mode") val mode: String? = null,
    @SerializedName("modeLabel") val modeLabel: String? = null,
    @SerializedName("managementAp") val managementAp: ManagementApStatus? = null,
    @SerializedName("ethernet") val ethernet: EthernetStatus? = null,
    @SerializedName("interfaces") val interfaces: NetworkInterfaces? = null,
)

data class NetworkInterfaces(
    @SerializedName("managementAp") val managementAp: ManagementApStatus? = null,
    @SerializedName("ethernet") val ethernet: EthernetStatus? = null,
)

data class ManagementApStatus(
    @SerializedName("enabled") val enabled: Boolean = false,
    @SerializedName("running") val running: Boolean = false,
    @SerializedName("ssid") val ssid: String? = null,
    @SerializedName("ip") val ip: String? = null,
    @SerializedName("mode") val mode: String? = null,
    @SerializedName("clients") val clients: Int? = null,
    @SerializedName("connectedClients") val connectedClients: Int? = null,
    @SerializedName("uptimeSeconds") val uptimeSeconds: Long? = null,
    @SerializedName("timeoutSeconds") val timeoutSeconds: Int? = null,
    @SerializedName("security") val security: String? = null,
)

data class EthernetStatus(
    @SerializedName("link") val link: Boolean = false,
    @SerializedName("linkUp") val linkUp: Boolean = false,
    @SerializedName("ip") val ip: String? = null,
    @SerializedName("gateway") val gateway: String? = null,
    @SerializedName("subnet") val subnet: String? = null,
    @SerializedName("dns") val dns: String? = null,
    @SerializedName("mac") val mac: String? = null,
)

fun NetworkStatusData.resolvedManagementAp(): ManagementApStatus? =
    managementAp ?: interfaces?.managementAp

fun NetworkStatusData.resolvedEthernet(): EthernetStatus? =
    ethernet ?: interfaces?.ethernet
