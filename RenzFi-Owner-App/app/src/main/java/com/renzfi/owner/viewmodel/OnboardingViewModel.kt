package com.renzfi.owner.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.data.repository.ProvisioningRepository
import com.renzfi.owner.model.ApplianceDefaults
import com.renzfi.owner.model.ApplianceProfile
import com.renzfi.owner.model.ConnectRouterRequest
import com.renzfi.owner.model.NetworkMode
import com.renzfi.owner.model.NetworkStatusData
import com.renzfi.owner.model.ProvisioningApiException
import com.renzfi.owner.model.ProvisioningCheck
import com.renzfi.owner.model.SelectDriverRequest
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.model.WorkflowResponse
import com.renzfi.owner.model.mikrotikDetectedInResponse
import com.renzfi.owner.model.resolvedEthernet
import com.renzfi.owner.util.Constants
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

enum class OnboardingPhase {
    WifiInstructions,
    Detecting,
    Detected,
    Wizard,
    RejoinWifi,
    Discovering,
    Complete,
}

enum class WizardStep {
    Welcome,
    NetworkType,
    RouterConnection,
    PortalConfiguration,
    CoinConfiguration,
    Validation,
    Summary,
    Finish,
}

data class DetectedAppliance(
    val deviceId: String,
    val firmwareVersion: String,
    val hardwareRevision: String,
    val buildLabel: String,
    val setupSsidHint: String,
)

data class OnboardingDraft(
    val networkMode: NetworkMode = NetworkMode.MIKROTIK,
    val routerHost: String = "10.40.0.1",
    val routerUsername: String = "admin",
    val routerPassword: String = "",
    val keepManagementApEnabled: Boolean = false,
    val mikrotikDetected: Boolean = false,
)

data class OnboardingUiState(
    val phase: OnboardingPhase = OnboardingPhase.WifiInstructions,
    val wizardStep: WizardStep = WizardStep.Welcome,
    val workflow: WorkflowResponse? = null,
    val detected: DetectedAppliance? = null,
    val networkStatus: NetworkStatusData? = null,
    val validationChecks: List<ProvisioningCheck> = emptyList(),
    val validationPassed: Boolean = false,
    val draft: OnboardingDraft = OnboardingDraft(),
    val registeredDevice: VendoDevice? = null,
    val isLoading: Boolean = false,
    val errorMessage: String? = null,
    val discoveryProgress: String? = null,
    val preferredWifiSsid: String? = null,
    val registeredIp: String? = null,
)

class OnboardingViewModel(application: Application) : AndroidViewModel(application) {
    private val provisioningRepository = ProvisioningRepository()
    private val deviceRepository = DeviceRepository(application)

    private val _uiState = MutableStateFlow(OnboardingUiState())
    val uiState: StateFlow<OnboardingUiState> = _uiState.asStateFlow()

    private var pendingDeviceId: String? = null
    private var pendingEthernetIp: String? = null
    private var awaitingManagementWifiReturn = false
    private var awaitingLanWifiReturn = false

    fun onOpenManagementWifiSettings() {
        awaitingManagementWifiReturn = true
    }

    fun onOpenLanWifiSettings() {
        awaitingLanWifiReturn = true
    }

    fun clearError() {
        _uiState.update { it.copy(errorMessage = null) }
    }

    fun updateDraft(transform: (OnboardingDraft) -> OnboardingDraft) {
        _uiState.update { it.copy(draft = transform(it.draft)) }
    }

    fun onReturnedFromWifiSettings() {
        when (_uiState.value.phase) {
            OnboardingPhase.WifiInstructions,
            OnboardingPhase.Detecting,
            -> if (awaitingManagementWifiReturn) {
                awaitingManagementWifiReturn = false
                detectAppliance()
            }
            OnboardingPhase.RejoinWifi -> if (awaitingLanWifiReturn) {
                awaitingLanWifiReturn = false
                rediscoverOnLan()
            }
            else -> Unit
        }
    }

    fun detectAppliance(autoAdvance: Boolean = false) {
        viewModelScope.launch {
            _uiState.update {
                it.copy(
                    phase = OnboardingPhase.Detecting,
                    isLoading = true,
                    errorMessage = null,
                )
            }
            runCatching {
                val (health, profile) = provisioningRepository.probeHealth(Constants.MANAGEMENT_AP_IP)
                provisioningRepository.login()
                val workflow = runCatching { provisioningRepository.resumeInstallation() }
                    .getOrElse { provisioningRepository.beginInstallation() }
                val network = runCatching { provisioningRepository.fetchNetworkStatus() }.getOrNull()
                val buildLabel = health.build?.let { build ->
                    listOfNotNull(
                        build.firmwareVersion,
                        build.gitCommit?.take(7),
                    ).joinToString(" · ")
                }.orEmpty().ifBlank { profile.firmwareVersion }

                pendingDeviceId = profile.deviceId
                pendingEthernetIp = network?.resolvedEthernet()?.ip?.takeIf { it.isNotBlank() }

                val detected = DetectedAppliance(
                    deviceId = profile.deviceId,
                    firmwareVersion = profile.firmwareVersion,
                    hardwareRevision = profile.hardwareRevision,
                    buildLabel = buildLabel,
                    setupSsidHint = Constants.MANAGEMENT_AP_SSID,
                )

                Triple(detected, workflow, network)
            }.onSuccess { (detected, workflow, network) ->
                val step = mapWorkflowToWizardStep(workflow.workflowStep)
                val nextPhase = if (autoAdvance) OnboardingPhase.Wizard else OnboardingPhase.Detected
                _uiState.update {
                    it.copy(
                        phase = nextPhase,
                        detected = detected,
                        workflow = workflow,
                        networkStatus = network,
                        wizardStep = step,
                        isLoading = false,
                        draft = it.draft.copy(
                            mikrotikDetected = mikrotikDetectedInResponse(workflow),
                            networkMode = if (mikrotikDetectedInResponse(workflow)) {
                                NetworkMode.MIKROTIK
                            } else {
                                it.draft.networkMode
                            },
                        ),
                    )
                }
                if (autoAdvance) {
                    refreshNetworkStatus()
                }
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        phase = OnboardingPhase.WifiInstructions,
                        isLoading = false,
                        errorMessage = error.toUserMessage(
                            "Could not reach the appliance at ${Constants.MANAGEMENT_AP_IP}. " +
                                "Connect to \"${Constants.MANAGEMENT_AP_SSID}\" and try again.",
                        ),
                    )
                }
            }
        }
    }

    fun continueFromDetection() {
        _uiState.update { it.copy(phase = OnboardingPhase.Wizard) }
        refreshNetworkStatus()
    }

    fun refreshNetworkStatus() {
        viewModelScope.launch {
            runCatching { provisioningRepository.fetchNetworkStatus() }
                .onSuccess { status ->
                    pendingEthernetIp = status.resolvedEthernet()?.ip?.takeIf { ip -> ip.isNotBlank() }
                        ?: pendingEthernetIp
                    _uiState.update { it.copy(networkStatus = status) }
                }
        }
    }

    fun startOrResumeWizard() {
        viewModelScope.launch {
            setLoading(true)
            runCatching {
                val workflow = runCatching { provisioningRepository.resumeInstallation() }
                    .getOrElse { provisioningRepository.beginInstallation() }
                maybeRunBackgroundDetection()
                workflow
            }.onSuccess { workflow ->
                applyWorkflow(workflow)
            }.onFailure { error ->
                setError(error, "Unable to start setup.")
            }
            setLoading(false)
        }
    }

    fun submitNetworkType() {
        val mode = _uiState.value.draft.networkMode
        viewModelScope.launch {
            setLoading(true)
            runCatching {
                provisioningRepository.selectDriver(SelectDriverRequest(driverId = mode.driverId))
            }.onSuccess { workflow ->
                applyWorkflow(workflow)
            }.onFailure { error ->
                setError(error, "Unable to configure network type.")
            }
            setLoading(false)
        }
    }

    fun submitRouterConnection() {
        val draft = _uiState.value.draft
        viewModelScope.launch {
            setLoading(true)
            runCatching {
                provisioningRepository.connectRouter(
                    ConnectRouterRequest(
                        host = draft.routerHost.trim(),
                        username = if (draft.networkMode == NetworkMode.MIKROTIK) {
                            draft.routerUsername.trim()
                        } else {
                            ""
                        },
                        password = if (draft.networkMode == NetworkMode.MIKROTIK) {
                            draft.routerPassword
                        } else {
                            ""
                        },
                    ),
                )
            }.onSuccess { workflow ->
                if (workflow.connected == true || workflow.ok != false) {
                    applyWorkflow(workflow)
                } else {
                    setError(null, workflow.error ?: "Connection test failed.")
                }
            }.onFailure { error ->
                setError(error, "Connection test failed.")
            }
            setLoading(false)
        }
    }

    fun submitPortalConfiguration() {
        viewModelScope.launch {
            setLoading(true)
            runCatching {
                provisioningRepository.configurePortal(ApplianceDefaults.portalBody())
            }.onSuccess { workflow ->
                if (workflow.verified == true || workflow.ok != false) {
                    applyWorkflow(workflow)
                } else {
                    setError(null, workflow.error ?: "Portal configuration failed.")
                }
            }.onFailure { error ->
                setError(error, "Portal configuration failed.")
            }
            setLoading(false)
        }
    }

    fun submitCoinConfiguration(skip: Boolean = false) {
        viewModelScope.launch {
            setLoading(true)
            runCatching {
                if (skip) {
                    provisioningRepository.configureCoin(mapOf("skip" to true))
                } else {
                    provisioningRepository.configureCoin(ApplianceDefaults.coinBody())
                }
            }.onSuccess { workflow ->
                if (workflow.skipped == true || workflow.ok != false) {
                    applyWorkflow(workflow)
                } else {
                    setError(null, workflow.error ?: "Coin configuration failed.")
                }
            }.onFailure { error ->
                setError(error, "Coin configuration failed.")
            }
            setLoading(false)
        }
    }

    fun runValidation() {
        viewModelScope.launch {
            setLoading(true)
            runCatching { provisioningRepository.validate() }
                .onSuccess { workflow ->
                    val passed = workflow.passed == true && workflow.ok != false
                    _uiState.update {
                        it.copy(
                            validationChecks = workflow.checks.orEmpty(),
                            validationPassed = passed,
                            workflow = workflow,
                            wizardStep = if (passed) {
                                mapWorkflowToWizardStep(workflow.workflowStep)
                            } else {
                                WizardStep.Validation
                            },
                            errorMessage = if (passed) null else workflow.error ?: "One or more checks failed.",
                        )
                    }
                }
                .onFailure { error ->
                    setError(error, "Unable to run installation checks.")
                }
            setLoading(false)
        }
    }

    fun completeSetup() {
        val keepEnabled = _uiState.value.draft.keepManagementApEnabled
        viewModelScope.launch {
            setLoading(true)
            runCatching {
                val workflow = provisioningRepository.finish()
                if (workflow.finished != true && workflow.ok == false) {
                    throw ProvisioningApiException(workflow.error ?: "Unable to finalize installation.")
                }
                provisioningRepository.managementApPostSetup(keepEnabled)
                workflow
            }.onSuccess { workflow ->
                provisioningRepository.clearSession()
                val preferredWifi = deviceRepository.getLastOperationalWifiSsid()
                _uiState.update {
                    it.copy(
                        phase = OnboardingPhase.RejoinWifi,
                        workflow = workflow,
                        wizardStep = WizardStep.Finish,
                        isLoading = false,
                        errorMessage = null,
                        preferredWifiSsid = preferredWifi,
                        registeredIp = null,
                    )
                }
            }.onFailure { error ->
                setError(error, "Unable to finalize installation.")
                setLoading(false)
            }
        }
    }

    fun rediscoverOnLan() {
        viewModelScope.launch {
            _uiState.update {
                it.copy(
                    phase = OnboardingPhase.Discovering,
                    isLoading = true,
                    errorMessage = null,
                    discoveryProgress = "Scanning local network…",
                )
            }

            deviceRepository.captureOperationalWifiHint(getApplication())

            val targetId = pendingDeviceId
            val knownIp = pendingEthernetIp

            runCatching {
                if (!knownIp.isNullOrBlank()) {
                    deviceRepository.probeAndRegisterIp(knownIp)?.let { return@runCatching it }
                }
                if (!targetId.isNullOrBlank()) {
                    val subnet = Constants.DEFAULT_DISCOVERY_SUBNET
                    val found = deviceRepository.discoverDevicesOnSubnet(subnet) { progress ->
                        _uiState.update {
                            it.copy(
                                discoveryProgress = "Scanning ${progress.scanned}/${progress.total}…",
                            )
                        }
                    }.firstOrNull { device ->
                        device.applianceDeviceId == targetId || device.id == targetId
                    }
                    if (found != null) return@runCatching found
                }
                deviceRepository.discoverDevicesOnSubnet().firstOrNull()
                    ?: throw ProvisioningApiException(
                        "Appliance not found on your network. Reconnect to Wi-Fi and try again.",
                    )
            }.onSuccess { device ->
                _uiState.update {
                    it.copy(
                        phase = OnboardingPhase.Complete,
                        registeredDevice = device,
                        registeredIp = device.esp32LocalIp,
                        isLoading = false,
                        discoveryProgress = null,
                        errorMessage = null,
                    )
                }
            }.onFailure { error ->
                _uiState.update {
                    it.copy(
                        phase = OnboardingPhase.RejoinWifi,
                        isLoading = false,
                        discoveryProgress = null,
                        errorMessage = error.toUserMessage(
                            "Appliance not found. Reconnect to your normal Wi-Fi and try again.",
                        ),
                    )
                }
            }
        }
    }

    fun goToFinishStep() {
        _uiState.update { it.copy(wizardStep = WizardStep.Finish) }
    }

    fun cancelOnboarding() {
        provisioningRepository.clearSession()
        pendingDeviceId = null
        pendingEthernetIp = null
        awaitingManagementWifiReturn = false
        awaitingLanWifiReturn = false
        _uiState.value = OnboardingUiState()
    }

    private suspend fun maybeRunBackgroundDetection() {
        runCatching { provisioningRepository.detectRouters() }
            .onSuccess { detect ->
                if (mikrotikDetectedInResponse(detect)) {
                    _uiState.update {
                        it.copy(
                            draft = it.draft.copy(
                                mikrotikDetected = true,
                                networkMode = NetworkMode.MIKROTIK,
                            ),
                        )
                    }
                }
            }
    }

    private fun applyWorkflow(workflow: WorkflowResponse) {
        _uiState.update {
            it.copy(
                workflow = workflow,
                wizardStep = mapWorkflowToWizardStep(workflow.workflowStep),
                errorMessage = null,
            )
        }
        refreshNetworkStatus()
    }

    private fun setLoading(loading: Boolean) {
        _uiState.update { it.copy(isLoading = loading, errorMessage = if (loading) null else it.errorMessage) }
    }

    private fun setError(error: Throwable?, fallback: String) {
        _uiState.update {
            it.copy(
                isLoading = false,
                errorMessage = error.toUserMessage(fallback),
            )
        }
    }

    private fun Throwable?.toUserMessage(fallback: String): String = when (this) {
        is ProvisioningApiException -> message ?: fallback
        null -> fallback
        else -> localizedMessage ?: fallback
    }

    companion object {
        fun mapWorkflowToWizardStep(workflowStep: String?): WizardStep = when (workflowStep) {
            "router_detection", "driver_selection" -> WizardStep.NetworkType
            "router_connection" -> WizardStep.RouterConnection
            "portal_configuration" -> WizardStep.PortalConfiguration
            "coin_configuration" -> WizardStep.CoinConfiguration
            "validation" -> WizardStep.Validation
            "summary" -> WizardStep.Summary
            "ready" -> WizardStep.Finish
            else -> WizardStep.Welcome
        }
    }
}
