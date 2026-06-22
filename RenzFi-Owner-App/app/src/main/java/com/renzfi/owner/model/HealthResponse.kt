package com.renzfi.owner.model

import com.google.gson.annotations.SerializedName

data class HealthResponse(
    @SerializedName("success") val success: Boolean = false,
    @SerializedName("message") val message: String? = null,
    @SerializedName("data") val data: HealthData? = null,
)

data class HealthData(
    @SerializedName("ok") val ok: Boolean = false,
    @SerializedName("storage") val storage: StorageHealth? = null,
    @SerializedName("session") val session: SessionHealth? = null,
)

data class StorageHealth(
    @SerializedName("ok") val ok: Boolean = false,
)

data class SessionHealth(
    @SerializedName("authenticated") val authenticated: Boolean = false,
    @SerializedName("mustChangePassword") val mustChangePassword: Boolean = false,
)
