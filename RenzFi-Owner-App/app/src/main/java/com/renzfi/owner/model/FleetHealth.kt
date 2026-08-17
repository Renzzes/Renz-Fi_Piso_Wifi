package com.renzfi.owner.model

/**
 * Client-computed fleet health — mirrors browser [fleetHealthService.ts].
 * Firmware returns facts via GET /api/health; the app computes level/score.
 */
enum class FleetHealthLevel {
    Healthy,
    Warning,
    Offline,
}

data class FleetApplianceHealth(
    val deviceId: String,
    val level: FleetHealthLevel,
    val score: Int,
    val warnings: List<String>,
    val snapshot: ApplianceHealthSnapshot?,
)

data class ApplianceHealthSnapshot(
    val probeOk: Boolean,
    val fetchedAtMs: Long,
    val profile: ApplianceProfile?,
    val storageOk: Boolean,
    val sdMounted: Boolean,
    val fallbackActive: Boolean,
    val spiffsReady: Boolean,
    val storageMode: String?,
    val installationState: String?,
    val installationReady: Boolean,
    val installationNeedsSetup: Boolean,
    val installationProgress: Int,
    val routerConfigured: Boolean,
    val routerDriverId: String?,
    val portalRevision: Int,
    val portalHasBanner: Boolean,
    val coinEnabled: Boolean,
    val coinOk: Boolean,
    val coinFault: Boolean,
    val uptimeSeconds: Long?,
    val deviceProfileVersion: Int?,
    val storageContractVersion: Int?,
    val httpContractVersion: Int?,
    val sessionAuthenticated: Boolean,
)

object FleetHealthCalculator {
    fun compute(deviceOnline: Boolean, snapshot: ApplianceHealthSnapshot?): FleetApplianceHealth {
        val deviceId = snapshot?.profile?.deviceId ?: "unknown"
        val warnings = mutableListOf<String>()

        if (snapshot == null || !snapshot.probeOk || !deviceOnline || snapshot.profile?.online != true) {
            return FleetApplianceHealth(
                deviceId = deviceId,
                level = FleetHealthLevel.Offline,
                score = 0,
                warnings = listOf("Appliance unreachable"),
                snapshot = snapshot,
            )
        }

        if (!snapshot.storageOk) warnings.add("Storage degraded")
        if (snapshot.fallbackActive) warnings.add("SD fallback active (SPIFFS)")
        if (!snapshot.spiffsReady) warnings.add("SPIFFS not ready")

        if (snapshot.installationNeedsSetup && !snapshot.installationReady) {
            warnings.add("Setup incomplete (${snapshot.installationState ?: "unknown"})")
        }

        if (snapshot.profile?.routerDriver != null && !snapshot.routerConfigured) {
            warnings.add("Router not configured")
        }

        if (snapshot.installationReady && snapshot.portalRevision == 0 && !snapshot.portalHasBanner) {
            warnings.add("Portal assets incomplete")
        }

        if (snapshot.coinEnabled && snapshot.coinFault) warnings.add("Coin hardware fault")
        if (snapshot.coinEnabled && !snapshot.coinOk) warnings.add("Coin hardware not ready")

        return if (warnings.isNotEmpty()) {
            FleetApplianceHealth(
                deviceId = deviceId,
                level = FleetHealthLevel.Warning,
                score = maxOf(35, 100 - warnings.size * 15),
                warnings = warnings,
                snapshot = snapshot,
            )
        } else {
            FleetApplianceHealth(
                deviceId = deviceId,
                level = FleetHealthLevel.Healthy,
                score = 100,
                warnings = emptyList(),
                snapshot = snapshot,
            )
        }
    }

    fun emoji(level: FleetHealthLevel): String = when (level) {
        FleetHealthLevel.Healthy -> "🟢"
        FleetHealthLevel.Warning -> "🟡"
        FleetHealthLevel.Offline -> "🔴"
    }

    fun label(level: FleetHealthLevel): String = when (level) {
        FleetHealthLevel.Healthy -> "Healthy"
        FleetHealthLevel.Warning -> "Warning"
        FleetHealthLevel.Offline -> "Offline"
    }
}

fun HealthData.toApplianceHealthSnapshot(fallbackIp: String, fetchedAtMs: Long = System.currentTimeMillis()): ApplianceHealthSnapshot? {
    val profile = toApplianceProfile(fallbackIp) ?: return null
    val build = build
    return ApplianceHealthSnapshot(
        probeOk = ok,
        fetchedAtMs = fetchedAtMs,
        profile = profile,
        storageOk = storage?.ok != false,
        sdMounted = storage?.sdMounted == true,
        fallbackActive = storage?.fallbackActive == true,
        spiffsReady = storage?.spiffsReady == true,
        storageMode = storage?.storageMode,
        installationState = installation?.state,
        installationReady = installation?.ready == true,
        installationNeedsSetup = installation?.needsSetup == true,
        installationProgress = installation?.progressPercent ?: 0,
        routerConfigured = router?.configured == true,
        routerDriverId = router?.driverId,
        portalRevision = portal?.revision ?: 0,
        portalHasBanner = portal?.hasBanner == true,
        coinEnabled = coin?.enabled == true,
        coinOk = coin?.enabled == true && coin?.state != "fault",
        coinFault = coin?.state == "fault" || coin?.faultReason != null,
        uptimeSeconds = uptimeSeconds,
        deviceProfileVersion = build?.deviceProfileVersion,
        storageContractVersion = build?.storageContractVersion,
        httpContractVersion = build?.httpContractVersion,
        sessionAuthenticated = session?.authenticated == true,
    )
}
