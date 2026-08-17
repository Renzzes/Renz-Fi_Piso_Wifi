package com.renzfi.owner.model

/**
 * Default portal/coin bodies sent to provisioning APIs.
 * Mirrors src/lib/applianceConfiguration.ts — firmware owns validation.
 */
object ApplianceDefaults {
    fun portalBody(): Map<String, Any> = mapOf(
        "portal" to mapOf(
            "portalName" to "Renz-Fi Piso WiFi",
            "welcomeMessage" to "Welcome! Insert a coin or enter a voucher to connect.",
            "footerText" to "Thank you for using our service!",
            "theme" to "default",
            "language" to "en",
            "enableVoucher" to true,
            "enableCoin" to true,
            "autoPlayMusic" to true,
            "showPauseButton" to true,
            "showTerminateButton" to true,
        ),
    )

    fun coinBody(): Map<String, Any> = mapOf(
        "coin" to mapOf(
            "enabled" to true,
            "pulsesPerPeso" to 1,
            "debounceMs" to 35,
            "settleMs" to 450,
            "timeoutSeconds" to 60,
            "defaultMinutesPerPeso" to 5,
            "pricingProfile" to "Default promo rates",
        ),
    )
}
