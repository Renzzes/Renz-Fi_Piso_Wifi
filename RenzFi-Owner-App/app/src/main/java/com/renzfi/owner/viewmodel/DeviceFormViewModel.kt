package com.renzfi.owner.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.util.Constants
import com.renzfi.owner.util.NetworkUtils
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class DeviceFormUiState(
    val deviceId: String? = null,
    val name: String = "",
    val mikrotikDisplayName: String = "",
    val mikrotikDdns: String = "",
    val mikrotikPublicIp: String = "",
    val mikrotikNotes: String = "",
    val esp32LocalIp: String = Constants.DEFAULT_ESP32_IP,
    val discoverySubnet: String = Constants.DEFAULT_DISCOVERY_SUBNET,
    val isSaving: Boolean = false,
    val isDiscovering: Boolean = false,
    val discoveryMessage: String? = null,
    val saveSuccess: Boolean = false,
    val errorMessage: String? = null,
)

class DeviceFormViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = DeviceRepository(application)

    private val _uiState = MutableStateFlow(DeviceFormUiState())
    val uiState: StateFlow<DeviceFormUiState> = _uiState.asStateFlow()

    fun loadDevice(deviceId: String?) {
        if (deviceId.isNullOrBlank()) {
            _uiState.value = DeviceFormUiState()
            return
        }
        viewModelScope.launch {
            val device = repository.getDevice(deviceId)
            if (device != null) {
                _uiState.value = DeviceFormUiState(
                    deviceId = device.id,
                    name = device.name,
                    mikrotikDisplayName = device.mikrotikDisplayName,
                    mikrotikDdns = device.mikrotikDdns,
                    mikrotikPublicIp = device.mikrotikPublicIp,
                    mikrotikNotes = device.mikrotikNotes,
                    esp32LocalIp = device.esp32LocalIp,
                )
            }
        }
    }

    fun updateName(value: String) {
        _uiState.value = _uiState.value.copy(name = value, errorMessage = null)
    }

    fun updateMikrotikDisplayName(value: String) {
        _uiState.value = _uiState.value.copy(mikrotikDisplayName = value, errorMessage = null)
    }

    fun updateMikrotikDdns(value: String) {
        _uiState.value = _uiState.value.copy(mikrotikDdns = value, errorMessage = null)
    }

    fun updateMikrotikPublicIp(value: String) {
        _uiState.value = _uiState.value.copy(mikrotikPublicIp = value, errorMessage = null)
    }

    fun updateMikrotikNotes(value: String) {
        _uiState.value = _uiState.value.copy(mikrotikNotes = value, errorMessage = null)
    }

    fun updateEsp32LocalIp(value: String) {
        _uiState.value = _uiState.value.copy(esp32LocalIp = value, errorMessage = null)
    }

    fun updateDiscoverySubnet(value: String) {
        _uiState.value = _uiState.value.copy(discoverySubnet = value, errorMessage = null)
    }

    fun discoverAtCurrentIp() {
        val ip = _uiState.value.esp32LocalIp.trim()
        if (!NetworkUtils.isValidHost(ip)) {
            _uiState.value = _uiState.value.copy(errorMessage = "Enter a valid IP to probe.")
            return
        }
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isDiscovering = true, discoveryMessage = null)
            try {
                val device = repository.probeAndRegisterIp(ip)
                if (device != null) {
                    _uiState.value = _uiState.value.copy(
                        deviceId = device.id,
                        name = device.name,
                        esp32LocalIp = device.esp32LocalIp,
                        discoveryMessage = "Found ${device.name} (${device.applianceDeviceId ?: device.id})",
                        saveSuccess = true,
                    )
                } else {
                    _uiState.value = _uiState.value.copy(
                        errorMessage = "No Renz-Fi appliance at $ip.",
                    )
                }
            } catch (_: Exception) {
                _uiState.value = _uiState.value.copy(errorMessage = "Discovery failed.")
            } finally {
                _uiState.value = _uiState.value.copy(isDiscovering = false)
            }
        }
    }

    fun discoverSubnet() {
        val subnet = _uiState.value.discoverySubnet.trim()
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(
                isDiscovering = true,
                discoveryMessage = "Scanning subnet…",
                errorMessage = null,
            )
            try {
                val found = repository.discoverDevicesOnSubnet(subnet) { progress ->
                    _uiState.value = _uiState.value.copy(
                        discoveryMessage = "Scanning ${progress.scanned}/${progress.total}…",
                    )
                }
                if (found.isEmpty()) {
                    _uiState.value = _uiState.value.copy(
                        errorMessage = "No appliances found on $subnet.",
                        discoveryMessage = null,
                    )
                } else {
                    val first = found.first()
                    _uiState.value = _uiState.value.copy(
                        deviceId = first.id,
                        name = first.name,
                        esp32LocalIp = first.esp32LocalIp,
                        discoveryMessage = "Discovered ${found.size} appliance(s).",
                        saveSuccess = true,
                    )
                }
            } catch (_: Exception) {
                _uiState.value = _uiState.value.copy(errorMessage = "Subnet discovery failed.")
            } finally {
                _uiState.value = _uiState.value.copy(isDiscovering = false)
            }
        }
    }

    fun save() {
        val state = _uiState.value
        val name = state.name.trim()
        val esp32Ip = state.esp32LocalIp.trim()

        if (name.isEmpty()) {
            _uiState.value = state.copy(errorMessage = "Device name is required.")
            return
        }
        if (!NetworkUtils.isValidHost(esp32Ip)) {
            _uiState.value = state.copy(
                errorMessage = "Enter a valid ESP32 IP address (e.g. ${Constants.DEFAULT_ESP32_IP}).",
            )
            return
        }
        if (state.mikrotikPublicIp.isNotBlank() && !NetworkUtils.isValidHost(state.mikrotikPublicIp.trim())) {
            _uiState.value = state.copy(errorMessage = "Enter a valid public IP address.")
            return
        }

        viewModelScope.launch {
            _uiState.value = state.copy(isSaving = true, errorMessage = null)
            try {
                val existingId = state.deviceId
                if (existingId != null) {
                    val existing = repository.getDevice(existingId)
                    if (existing != null) {
                        repository.updateDevice(
                            existing.copy(
                                name = name,
                                mikrotikDisplayName = state.mikrotikDisplayName.trim(),
                                mikrotikDdns = state.mikrotikDdns.trim(),
                                mikrotikPublicIp = state.mikrotikPublicIp.trim(),
                                mikrotikNotes = state.mikrotikNotes.trim(),
                                esp32LocalIp = esp32Ip,
                            ),
                        )
                    }
                } else {
                    repository.addDevice(
                        VendoDevice(
                            name = name,
                            mikrotikDisplayName = state.mikrotikDisplayName.trim(),
                            mikrotikDdns = state.mikrotikDdns.trim(),
                            mikrotikPublicIp = state.mikrotikPublicIp.trim(),
                            mikrotikNotes = state.mikrotikNotes.trim(),
                            esp32LocalIp = esp32Ip,
                        ),
                    )
                }
                _uiState.value = _uiState.value.copy(isSaving = false, saveSuccess = true)
            } catch (_: Exception) {
                _uiState.value = _uiState.value.copy(
                    isSaving = false,
                    errorMessage = "Failed to save device. Please try again.",
                )
            }
        }
    }

    fun clearSaveSuccess() {
        _uiState.value = _uiState.value.copy(saveSuccess = false)
    }
}
