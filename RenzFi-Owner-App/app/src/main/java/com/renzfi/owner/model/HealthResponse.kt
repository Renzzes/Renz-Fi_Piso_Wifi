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
    @SerializedName("installation") val installation: InstallationHealth? = null,
    @SerializedName("router") val router: RouterHealth? = null,
    @SerializedName("portal") val portal: PortalHealth? = null,
    @SerializedName("coin") val coin: CoinHealth? = null,
    @SerializedName("build") val build: BuildHealth? = null,
    @SerializedName("uptimeSeconds") val uptimeSeconds: Long? = null,
    @SerializedName("serverTimeMs") val serverTimeMs: Long? = null,
    @SerializedName("device") val device: DeviceHealthProfile? = null,
    @SerializedName("deviceId") val deviceId: String? = null,
    @SerializedName("deviceName") val deviceName: String? = null,
    @SerializedName("version") val version: String? = null,
    @SerializedName("managementAp") val managementAp: ManagementApHealth? = null,
)

data class ManagementApHealth(
    @SerializedName("enabled") val enabled: Boolean = false,
    @SerializedName("running") val running: Boolean = false,
    @SerializedName("mode") val mode: String? = null,
    @SerializedName("ssid") val ssid: String? = null,
    @SerializedName("ip") val ip: String? = null,
)

data class DeviceCapabilities(
    @SerializedName("coin") val coin: Boolean = false,
    @SerializedName("voucher") val voucher: Boolean = true,
    @SerializedName("assetUpload") val assetUpload: Boolean = true,
    @SerializedName("router") val router: String? = null,
    @SerializedName("fleet") val fleet: Boolean = true,
)

data class DeviceHealthProfile(
    @SerializedName("deviceId") val deviceId: String? = null,
    @SerializedName("serialNumber") val serialNumber: String? = null,
    @SerializedName("friendlyName") val friendlyName: String? = null,
    @SerializedName("deviceName") val deviceName: String? = null,
    @SerializedName("firmwareVersion") val firmwareVersion: String? = null,
    @SerializedName("version") val version: String? = null,
    @SerializedName("hardwareRevision") val hardwareRevision: String? = null,
    @SerializedName("macAddress") val macAddress: String? = null,
    @SerializedName("ipAddress") val ipAddress: String? = null,
    @SerializedName("routerDriver") val routerDriver: String? = null,
    @SerializedName("online") val online: Boolean? = null,
    @SerializedName("capabilities") val capabilities: DeviceCapabilities? = null,
)

data class StorageHealth(
    @SerializedName("ok") val ok: Boolean = false,
    @SerializedName("sdMounted") val sdMounted: Boolean = false,
    @SerializedName("sdPresent") val sdPresent: Boolean = false,
    @SerializedName("fallbackActive") val fallbackActive: Boolean = false,
    @SerializedName("storageMode") val storageMode: String? = null,
    @SerializedName("spiffsReady") val spiffsReady: Boolean = false,
)

data class InstallationHealth(
    @SerializedName("state") val state: String? = null,
    @SerializedName("ready") val ready: Boolean = false,
    @SerializedName("needsSetup") val needsSetup: Boolean = false,
    @SerializedName("progressPercent") val progressPercent: Int = 0,
)

data class RouterProductHealth(
    @SerializedName("name") val name: String? = null,
    @SerializedName("subtitle") val subtitle: String? = null,
)

/** UI-friendly gateway feature flags from GET /api/health → router.capabilities. */
data class RouterCapabilitiesHealth(
    @SerializedName("hotspot") val hotspot: Boolean = false,
    @SerializedName("pauseResume") val pauseResume: Boolean = false,
    @SerializedName("bandwidthControl") val bandwidthControl: Boolean = false,
    @SerializedName("voucherSync") val voucherSync: Boolean = false,
    @SerializedName("queueManagement") val queueManagement: Boolean = false,
)

data class RouterHealth(
    @SerializedName("configured") val configured: Boolean = false,
    @SerializedName("driverId") val driverId: String? = null,
    @SerializedName("product") val product: RouterProductHealth? = null,
    /** detected | connected | unavailable */
    @SerializedName("status") val status: String? = null,
    @SerializedName("capabilities") val capabilities: RouterCapabilitiesHealth? = null,
)

data class PortalHealth(
    @SerializedName("revision") val revision: Int = 0,
    @SerializedName("hasBanner") val hasBanner: Boolean = false,
    @SerializedName("hasMusic") val hasMusic: Boolean = false,
    @SerializedName("assetsReady") val assetsReady: Boolean = false,
)

data class CoinHealth(
    @SerializedName("enabled") val enabled: Boolean = false,
    @SerializedName("state") val state: String? = null,
    @SerializedName("ok") val ok: Boolean = false,
    @SerializedName("fault") val fault: Boolean = false,
    @SerializedName("faultReason") val faultReason: String? = null,
)

data class BuildHealth(
    @SerializedName("firmwareVersion") val firmwareVersion: String? = null,
    @SerializedName("deviceProfileVersion") val deviceProfileVersion: Int? = null,
    @SerializedName("storageContractVersion") val storageContractVersion: Int? = null,
    @SerializedName("httpContractVersion") val httpContractVersion: Int? = null,
    @SerializedName("adminBuild") val adminBuild: String? = null,
    @SerializedName("buildNumber") val buildNumber: Int? = null,
    @SerializedName("gitCommit") val gitCommit: String? = null,
    @SerializedName("portalRevision") val portalRevision: String? = null,
)

data class SessionHealth(
    @SerializedName("authenticated") val authenticated: Boolean = false,
    @SerializedName("mustChangePassword") val mustChangePassword: Boolean = false,
)

/** Parsed permanent appliance identity from GET /api/health. */
data class ApplianceProfile(
    val deviceId: String,
    val friendlyName: String,
    val serialNumber: String,
    val firmwareVersion: String,
    val hardwareRevision: String,
    val macAddress: String,
    val ipAddress: String,
    val routerDriver: String?,
    val capabilities: DeviceCapabilities,
    val online: Boolean,
)

fun HealthData.toApplianceProfile(fallbackIp: String): ApplianceProfile? {
    val nested = device
    val id = nested?.deviceId?.takeIf { it.isNotBlank() }
        ?: deviceId?.takeIf { it.isNotBlank() }
        ?: return null
    val name = nested?.friendlyName?.takeIf { it.isNotBlank() }
        ?: nested?.deviceName?.takeIf { it.isNotBlank() }
        ?: deviceName?.takeIf { it.isNotBlank() }
        ?: "Renz-Fi Appliance"
    val driver = nested?.routerDriver?.takeIf { it.isNotBlank() }
    val caps = nested?.capabilities ?: DeviceCapabilities(router = driver)
    return ApplianceProfile(
        deviceId = id,
        friendlyName = name,
        serialNumber = nested?.serialNumber?.takeIf { it.isNotBlank() } ?: id,
        firmwareVersion = nested?.firmwareVersion?.takeIf { it.isNotBlank() }
            ?: nested?.version?.takeIf { it.isNotBlank() }
            ?: version.orEmpty(),
        hardwareRevision = nested?.hardwareRevision.orEmpty(),
        macAddress = nested?.macAddress.orEmpty(),
        ipAddress = nested?.ipAddress?.takeIf { it.isNotBlank() } ?: fallbackIp,
        routerDriver = driver,
        capabilities = caps.copy(router = caps.router ?: driver),
        online = nested?.online != false,
    )
}
