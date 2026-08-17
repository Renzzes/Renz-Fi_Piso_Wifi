package com.renzfi.owner.model

/**
 * Snapshot from a nearby appliance reachable at the Management AP address.
 */
data class NearbyApplianceInfo(
    val deviceId: String,
    val firmwareVersion: String,
    val hardwareRevision: String,
    val buildLabel: String,
    val managementApMode: String?,
    val needsSetup: Boolean,
    val connectedSsid: String?,
    val buildDetails: ApplianceBuildDetails,
    val readiness: ApplianceReadinessSummary,
)

sealed class NearbyApplianceEvaluation {
    data class NewAppliance(val info: NearbyApplianceInfo) : NearbyApplianceEvaluation()

    data class AlreadyRegistered(
        val device: VendoDevice,
        val info: NearbyApplianceInfo,
    ) : NearbyApplianceEvaluation()
}

/** App startup: an appliance on the setup network needs attention before normal fleet flow. */
sealed class StartupDiscovery {
    data object None : StartupDiscovery()

    /** A genuinely factory / unregistered appliance that needs first-time setup. */
    data class NewAppliance(val info: NearbyApplianceInfo) : StartupDiscovery()

    /**
     * An appliance that is already configured (not factory) was found nearby but is
     * NOT yet in the local device registry. The owner should add it through the
     * normal Add Appliance flow rather than being forced into onboarding.
     */
    data class FoundConfiguredAppliance(val info: NearbyApplianceInfo) : StartupDiscovery()

    /** A locally-registered appliance is reachable on the setup SSID. */
    data class AlreadyRegistered(
        val device: VendoDevice,
        val info: NearbyApplianceInfo,
    ) : StartupDiscovery()
}

fun HealthData.resolvedDeviceId(): String? =
    device?.deviceId?.takeIf { it.isNotBlank() } ?: deviceId?.takeIf { it.isNotBlank() }

fun HealthData.buildLabel(): String {
    val fromBuild = build?.let { b ->
        listOfNotNull(b.firmwareVersion, b.gitCommit?.take(7)).joinToString(" · ")
    }.orEmpty()
    if (fromBuild.isNotBlank()) return fromBuild
    return device?.firmwareVersion ?: device?.version ?: version.orEmpty()
}

fun HealthData.applianceNeedsSetup(): Boolean {
    val installation = installation
    if (installation?.needsSetup == true) return true
    if (installation?.ready == false) return true
    if (installation?.state == "factory") return true
    when (managementAp?.mode?.lowercase()) {
        "factory" -> return true
    }
    return false
}
