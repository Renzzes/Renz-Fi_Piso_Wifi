package com.renzfi.owner.model

import com.google.gson.annotations.SerializedName

data class InstallationSession(
    @SerializedName("sessionId") val sessionId: String? = null,
    @SerializedName("startedAt") val startedAt: Long? = null,
    @SerializedName("lastActivity") val lastActivity: Long? = null,
    @SerializedName("installerName") val installerName: String? = null,
    @SerializedName("deviceId") val deviceId: String? = null,
    @SerializedName("resumeToken") val resumeToken: String? = null,
    @SerializedName("isRecovery") val isRecovery: Boolean? = null,
    @SerializedName("attempt") val attempt: Int? = null,
    @SerializedName("elapsedMs") val elapsedMs: Long? = null,
    @SerializedName("elapsedMinutes") val elapsedMinutes: Int? = null,
)

data class InstallationStatus(
    @SerializedName("state") val state: String? = null,
    @SerializedName("updatedAt") val updatedAt: Long? = null,
    @SerializedName("firmwareVersion") val firmwareVersion: String? = null,
    @SerializedName("installationVersion") val installationVersion: Int? = null,
    @SerializedName("progressPercent") val progressPercent: Int = 0,
    @SerializedName("stepIndex") val stepIndex: Int? = null,
    @SerializedName("stepCount") val stepCount: Int? = null,
    @SerializedName("needsSetup") val needsSetup: Boolean? = null,
    @SerializedName("ready") val ready: Boolean? = null,
    @SerializedName("nextState") val nextState: String? = null,
    @SerializedName("previousState") val previousState: String? = null,
    @SerializedName("completedSteps") val completedSteps: List<String>? = null,
    @SerializedName("session") val session: InstallationSession? = null,
)

data class WorkflowResponse(
    @SerializedName("ok") val ok: Boolean? = null,
    @SerializedName("workflowStep") val workflowStep: String? = null,
    @SerializedName("installation") val installation: InstallationStatus? = null,
    @SerializedName("ready") val ready: Boolean? = null,
    @SerializedName("needsSetup") val needsSetup: Boolean? = null,
    @SerializedName("error") val error: String? = null,
    @SerializedName("resumed") val resumed: Boolean? = null,
    @SerializedName("resumePrompt") val resumePrompt: Boolean? = null,
    @SerializedName("started") val started: Boolean? = null,
    @SerializedName("alreadyReady") val alreadyReady: Boolean? = null,
    @SerializedName("driverId") val driverId: String? = null,
    @SerializedName("connected") val connected: Boolean? = null,
    @SerializedName("verified") val verified: Boolean? = null,
    @SerializedName("passed") val passed: Boolean? = null,
    @SerializedName("checks") val checks: List<ProvisioningCheck>? = null,
    @SerializedName("finished") val finished: Boolean? = null,
    @SerializedName("summary") val summary: Map<String, Any?>? = null,
    @SerializedName("skipped") val skipped: Boolean? = null,
    @SerializedName("drivers") val drivers: List<DriverDetectionEntry>? = null,
)

data class DriverDetectionEntry(
    @SerializedName("driverId") val driverId: String? = null,
    @SerializedName("detected") val detected: Boolean? = null,
    @SerializedName("configured") val configured: Boolean? = null,
)

data class ProvisioningCheck(
    @SerializedName("id") val id: String? = null,
    @SerializedName("passed") val passed: Boolean = false,
    @SerializedName("detail") val detail: String? = null,
    @SerializedName("severity") val severity: String? = null,
)

data class LoginRequest(
    @SerializedName("password") val password: String,
    @SerializedName("rememberIp") val rememberIp: Boolean = false,
)

data class LoginResponseData(
    @SerializedName("ok") val ok: Boolean? = null,
)

data class BeginInstallationRequest(
    @SerializedName("installerName") val installerName: String? = null,
    @SerializedName("isRecovery") val isRecovery: Boolean? = null,
)

data class SelectDriverRequest(
    @SerializedName("driverId") val driverId: String,
    @SerializedName("firmware") val firmware: String? = null,
    @SerializedName("version") val version: String? = null,
    @SerializedName("host") val host: String? = null,
    @SerializedName("username") val username: String? = null,
    @SerializedName("password") val password: String? = null,
    @SerializedName("profile") val profile: String? = null,
)

data class ConnectRouterRequest(
    @SerializedName("host") val host: String,
    @SerializedName("username") val username: String,
    @SerializedName("password") val password: String,
    @SerializedName("profile") val profile: String? = null,
)

data class ManagementApPostSetupRequest(
    @SerializedName("keepEnabled") val keepEnabled: Boolean,
)

enum class NetworkMode(val driverId: String, val label: String) {
    STANDARD("generic_ap", "Standard Network"),
    MIKROTIK("mikrotik", "MikroTik Enhanced"),
}

fun mikrotikDetectedInResponse(response: WorkflowResponse?): Boolean {
    val drivers = response?.drivers.orEmpty()
    return drivers.any {
        it.driverId == "mikrotik" && (it.detected == true || it.configured == true)
    }
}
