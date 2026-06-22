package com.renzfi.owner.model

import com.google.gson.annotations.SerializedName

/**
 * Future endpoint: GET /api/device/info
 * Gracefully ignored when unavailable.
 */
data class DeviceInfoResponse(
    @SerializedName("success") val success: Boolean = false,
    @SerializedName("data") val data: DeviceInfo? = null,
)

data class DeviceInfo(
    @SerializedName("deviceName") val deviceName: String? = null,
    @SerializedName("firmwareVersion") val firmwareVersion: String? = null,
    @SerializedName("serialNumber") val serialNumber: String? = null,
)
