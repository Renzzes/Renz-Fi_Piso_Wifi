package com.renzfi.owner.model

import com.renzfi.owner.util.ManagementApNetworkUtils
import com.renzfi.owner.util.ProductBranding
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/** Visual tone for a readiness line on the appliance summary. */
enum class ReadinessTone {
    Ready,
    Expected,
    Warning,
    Issue,
}

data class ReadinessCheck(
    val label: String,
    /** Optional detail — shown for warnings/issues, or inline for expected states. */
    val detail: String? = null,
    val tone: ReadinessTone,
)

/** Branded gateway product line from GET /api/health → router.product. */
data class GatewayProductStatus(
    val name: String,
    val subtitle: String?,
    val connectionLabel: String,
    val tone: ReadinessTone,
)

/** Enabled gateway features reported by the appliance — rendered dynamically. */
fun RouterCapabilitiesHealth.toFeatureChecks(): List<ReadinessCheck> = buildList {
    if (hotspot) add(ReadinessCheck("Hotspot", tone = ReadinessTone.Ready))
    if (pauseResume) add(ReadinessCheck("Pause / resume", tone = ReadinessTone.Ready))
    if (bandwidthControl) add(ReadinessCheck("Bandwidth control", tone = ReadinessTone.Ready))
    if (voucherSync) add(ReadinessCheck("Voucher sync", tone = ReadinessTone.Ready))
    if (queueManagement) add(ReadinessCheck("Queue management", tone = ReadinessTone.Ready))
}

/**
 * Hardware vs gateway readiness shown when an appliance is detected on the setup network.
 * Derived from GET /api/health plus phone Wi-Fi context.
 */
data class ApplianceReadinessSummary(
    val hardwareChecks: List<ReadinessCheck>,
    val gatewayProduct: GatewayProductStatus,
    val gatewayCapabilities: List<ReadinessCheck>,
    val gatewayChecks: List<ReadinessCheck>,
    val readyForInstallation: Boolean,
    val headline: String,
)

data class ApplianceBuildDetails(
    val adminBuild: String?,
    val buildNumber: Int?,
    val gitCommit: String?,
)

fun HealthData.toApplianceBuildDetails(): ApplianceBuildDetails {
    val meta = build
    return ApplianceBuildDetails(
        adminBuild = formatAdminBuildDate(meta?.adminBuild),
        buildNumber = meta?.buildNumber?.takeIf { it > 0 },
        gitCommit = meta?.gitCommit
            ?.takeIf { it.isNotBlank() && !it.equals("unknown", ignoreCase = true) },
    )
}

fun formatAdminBuildDate(raw: String?): String? {
    if (raw.isNullOrBlank()) return null
    return try {
        Instant.parse(raw)
            .atZone(ZoneId.systemDefault())
            .format(DateTimeFormatter.ISO_LOCAL_DATE)
    } catch (_: Exception) {
        raw.take(10).takeIf { it.length == 10 }
    }
}

fun HealthData.resolveGatewayProduct(): GatewayProductStatus {
    val product = router?.product
    val name = product?.name?.takeIf { it.isNotBlank() }
        ?: ProductBranding.GATEWAY_NAME
    val subtitle = product?.subtitle?.takeIf { it.isNotBlank() }
        ?: ProductBranding.GATEWAY_SUBTITLE

    val (connectionLabel, tone) = when (router?.status?.lowercase()) {
        "connected" -> "Connected" to ReadinessTone.Ready
        "detected" -> "Detected" to ReadinessTone.Ready
        "unavailable" -> "Not detected" to ReadinessTone.Issue
        else -> {
            val legacyDetected = !router?.driverId.isNullOrBlank() ||
                !device?.routerDriver.isNullOrBlank()
            if (legacyDetected) {
                if (router?.configured == true) "Connected" to ReadinessTone.Ready
                else "Detected" to ReadinessTone.Ready
            } else {
                "Not detected" to ReadinessTone.Issue
            }
        }
    }

    return GatewayProductStatus(
        name = name,
        subtitle = subtitle,
        connectionLabel = connectionLabel,
        tone = tone,
    )
}

fun HealthData.buildApplianceReadiness(connectedSsid: String?): ApplianceReadinessSummary {
    val storageHealth = storage
    val mgmtAp = managementAp
    val onSetupWifi = ManagementApNetworkUtils.isSetupSsid(connectedSsid)
    val mgmtApActive = mgmtAp?.running == true ||
        mgmtAp?.mode.equals("factory", ignoreCase = true) ||
        mgmtAp?.mode.equals("maintenance", ignoreCase = true)

    val firmwareOk = ok && !(
        device?.firmwareVersion.isNullOrBlank() &&
            device?.version.isNullOrBlank() &&
            version.isNullOrBlank() &&
            build?.firmwareVersion.isNullOrBlank()
        )

    val sdReady = storageHealth?.ok == true &&
        storageHealth.sdMounted &&
        !storageHealth.fallbackActive
    val sdPresent = storageHealth?.sdPresent == true
    val sdTone = when {
        sdReady -> ReadinessTone.Ready
        sdPresent -> ReadinessTone.Warning
        else -> ReadinessTone.Issue
    }
    val sdDetail = when {
        sdReady -> null
        sdPresent && storageHealth?.sdMounted != true -> "Check card"
        storageHealth?.fallbackActive == true -> "Fallback active"
        else -> "Not ready"
    }

    val portalReady = portal?.assetsReady == true ||
        (storageHealth?.spiffsReady == true && storageHealth.ok)
    val portalTone = if (portalReady) ReadinessTone.Ready else ReadinessTone.Issue

    val mgmtWifiOk = (onSetupWifi || mgmtApActive)
    val mgmtWifiTone = if (mgmtWifiOk) ReadinessTone.Ready else ReadinessTone.Warning
    val mgmtWifiDetail = if (mgmtWifiOk) null else "Verify connection"

    val gatewayProduct = resolveGatewayProduct()
    val gatewayCapabilities = router?.capabilities?.toFeatureChecks().orEmpty()

    val hardwareChecks = listOf(
        ReadinessCheck(
            label = "Appliance firmware",
            tone = if (firmwareOk) ReadinessTone.Ready else ReadinessTone.Issue,
            detail = if (firmwareOk) null else "Unavailable",
        ),
        ReadinessCheck(
            label = "SD card",
            tone = sdTone,
            detail = sdDetail,
        ),
        ReadinessCheck(
            label = "Portal assets",
            tone = portalTone,
            detail = if (portalReady) null else "Not ready",
        ),
        ReadinessCheck(
            label = "Management Wi-Fi",
            tone = mgmtWifiTone,
            detail = mgmtWifiDetail,
        ),
    )

    val gatewayChecks = listOf(
        ReadinessCheck(
            label = "Internet",
            tone = ReadinessTone.Expected,
            detail = when {
                router?.configured == true -> "Pending verification"
                else -> "Not connected yet"
            },
        ),
    )

    val blockers = hardwareChecks.count { it.tone == ReadinessTone.Issue } +
        if (gatewayProduct.tone == ReadinessTone.Issue) 1 else 0

    return ApplianceReadinessSummary(
        hardwareChecks = hardwareChecks,
        gatewayProduct = gatewayProduct,
        gatewayCapabilities = gatewayCapabilities,
        gatewayChecks = gatewayChecks,
        readyForInstallation = blockers == 0,
        headline = if (blockers == 0) {
            "Ready for installation"
        } else {
            "Resolve issues before setup"
        },
    )
}
