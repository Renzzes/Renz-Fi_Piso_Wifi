package com.renzfi.owner.model

sealed class ConnectionState {
    data object Idle : ConnectionState()
    data object Checking : ConnectionState()
    data class Connected(val device: VendoDevice) : ConnectionState()
    data class Failed(
        val message: String,
        val reason: FailureReason,
        val device: VendoDevice? = null,
    ) : ConnectionState()
    data object MultiDevice : ConnectionState()
}

enum class FailureReason {
    NO_NETWORK,
    UNREACHABLE,
    TIMEOUT,
    INVALID_RESPONSE,
    UNKNOWN,
}
