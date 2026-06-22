package com.renzfi.owner.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.model.VendoDevice
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class DeviceOverviewUiState(
    val device: VendoDevice? = null,
    val isLoading: Boolean = true,
)

class DeviceOverviewViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = DeviceRepository(application)

    private val _uiState = MutableStateFlow(DeviceOverviewUiState())
    val uiState: StateFlow<DeviceOverviewUiState> = _uiState.asStateFlow()

    fun loadDevice(deviceId: String) {
        viewModelScope.launch {
            _uiState.value = DeviceOverviewUiState(isLoading = true)
            val device = repository.getDevice(deviceId)
            _uiState.value = DeviceOverviewUiState(device = device, isLoading = false)
        }
    }

    fun refreshDevice(deviceId: String) {
        viewModelScope.launch {
            val device = repository.getDevice(deviceId) ?: return@launch
            repository.checkDeviceHealth(device)
            val updated = repository.getDevice(deviceId)
            _uiState.value = DeviceOverviewUiState(device = updated, isLoading = false)
        }
    }
}
