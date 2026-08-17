package com.renzfi.owner.model

import com.google.gson.annotations.SerializedName

/** Standard firmware JSON envelope — see PROVISIONING_API.md */
data class ApiEnvelope<T>(
    @SerializedName("success") val success: Boolean = false,
    @SerializedName("data") val data: T? = null,
    @SerializedName("message") val message: String? = null,
    @SerializedName("error") val error: String? = null,
    @SerializedName("code") val code: String? = null,
)

class ProvisioningApiException(
    message: String,
    val httpCode: Int = 0,
    val errorCode: String? = null,
) : Exception(message)
