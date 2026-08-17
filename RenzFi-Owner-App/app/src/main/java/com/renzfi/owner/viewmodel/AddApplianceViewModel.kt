package com.renzfi.owner.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.data.repository.ProvisioningRepository
import com.renzfi.owner.model.NearbyApplianceEvaluation
import com.renzfi.owner.util.Constants
import com.renzfi.owner.util.ManagementApNetworkUtils
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

enum class AddApplianceScanStatus {
    Idle,
    Scanning,
    Found,
    NotFound,
    OnSetupWifiUnreachable,
}

data class AddApplianceUiState(
    val scanStatus: AddApplianceScanStatus = AddApplianceScanStatus.Idle,
    val connectedSsid: String? = null,
    val detectedDeviceId: String? = null,
    val navigateToOnboarding: Boolean = false,
    val pendingAlreadyRegistered: NearbyApplianceEvaluation.AlreadyRegistered? = null,
    val errorMessage: String? = null,
)

class AddApplianceViewModel(application: Application) : AndroidViewModel(application) {
    private val deviceRepository = DeviceRepository(application)
    private val provisioningRepository = ProvisioningRepository()

    private val _uiState = MutableStateFlow(AddApplianceUiState())
    val uiState: StateFlow<AddApplianceUiState> = _uiState.asStateFlow()

    private var awaitingWifiReturn = false
    private var hasNavigated = false

    fun scanNearby() {
        if (_uiState.value.scanStatus == AddApplianceScanStatus.Scanning) return
        viewModelScope.launch {
            val context = getApplication<Application>()
            val ssid = ManagementApNetworkUtils.currentWifiSsid(context)
            _uiState.update {
                it.copy(
                    scanStatus = AddApplianceScanStatus.Scanning,
                    connectedSsid = ssid,
                    errorMessage = null,
                    pendingAlreadyRegistered = null,
                )
            }

            val onSetupSsid = ManagementApNetworkUtils.isSetupSsid(ssid)
            when (val evaluation = deviceRepository.evaluateNearbySetupAppliance(context)) {
                is NearbyApplianceEvaluation.AlreadyRegistered -> {
                    provisioningRepository.clearSession()
                    _uiState.update {
                        it.copy(
                            scanStatus = AddApplianceScanStatus.NotFound,
                            connectedSsid = ssid,
                            pendingAlreadyRegistered = evaluation,
                            errorMessage = null,
                        )
                    }
                }
                is NearbyApplianceEvaluation.NewAppliance -> {
                    provisioningRepository.clearSession()
                    if (!hasNavigated) {
                        hasNavigated = true
                        _uiState.update {
                            it.copy(
                                scanStatus = AddApplianceScanStatus.Found,
                                connectedSsid = ssid,
                                detectedDeviceId = evaluation.info.deviceId,
                                navigateToOnboarding = true,
                            )
                        }
                    }
                }
                null -> {
                    provisioningRepository.clearSession()
                    _uiState.update {
                        it.copy(
                            scanStatus = if (onSetupSsid) {
                                AddApplianceScanStatus.OnSetupWifiUnreachable
                            } else {
                                AddApplianceScanStatus.NotFound
                            },
                            connectedSsid = ssid,
                            errorMessage = if (onSetupSsid) {
                                "Connected to ${ssid ?: "setup Wi-Fi"} but the appliance is not responding at ${Constants.MANAGEMENT_AP_IP}."
                            } else {
                                null
                            },
                        )
                    }
                }
            }
        }
    }

    fun onOpenWifiSettings() {
        awaitingWifiReturn = true
    }

    fun onScreenResumed() {
        if (awaitingWifiReturn) {
            awaitingWifiReturn = false
            scanNearby()
        }
    }

    fun consumeNavigation() {
        _uiState.update { it.copy(navigateToOnboarding = false) }
    }

    fun consumeAlreadyRegisteredNavigation() {
        _uiState.update { it.copy(pendingAlreadyRegistered = null) }
    }

    fun resetNavigationGate() {
        hasNavigated = false
    }
}
