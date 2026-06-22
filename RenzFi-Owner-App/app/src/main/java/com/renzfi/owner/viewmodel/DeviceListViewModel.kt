package com.renzfi.owner.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.util.Constants
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

data class DeviceListUiState(
    val devices: List<VendoDevice> = emptyList(),
    val isRefreshing: Boolean = false,
    val isCheckingDevice: Boolean = false,
    val checkingDeviceId: String? = null,
    val errorMessage: String? = null,
)

class DeviceListViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = DeviceRepository(application)

    private val _uiState = MutableStateFlow(DeviceListUiState())
    val uiState: StateFlow<DeviceListUiState> = _uiState.asStateFlow()

    private var refreshJob: Job? = null

    init {
        viewModelScope.launch {
            repository.devices.collect { devices ->
                _uiState.value = _uiState.value.copy(devices = devices)
            }
        }
        startAutoRefresh()
    }

    fun refreshStatus() {
        if (_uiState.value.isRefreshing) return
        viewModelScope.launch {
            _uiState.value = _uiState.value.copy(isRefreshing = true, errorMessage = null)
            try {
                repository.refreshAllDeviceStatus(getApplication())
            } catch (_: Exception) {
                _uiState.value = _uiState.value.copy(
                    errorMessage = "Failed to refresh device status.",
                )
            } finally {
                _uiState.value = _uiState.value.copy(isRefreshing = false)
            }
        }
    }

    fun deleteDevice(id: String) {
        viewModelScope.launch {
            repository.deleteDevice(id)
        }
    }

    fun setCheckingDevice(deviceId: String?) {
        _uiState.value = _uiState.value.copy(
            isCheckingDevice = deviceId != null,
            checkingDeviceId = deviceId,
        )
    }

    fun clearCheckingDevice() {
        _uiState.value = _uiState.value.copy(
            isCheckingDevice = false,
            checkingDeviceId = null,
        )
    }

    private fun startAutoRefresh() {
        refreshJob?.cancel()
        refreshJob = viewModelScope.launch {
            while (isActive) {
                refreshStatus()
                delay(Constants.STATUS_REFRESH_INTERVAL_MS)
            }
        }
    }

    override fun onCleared() {
        refreshJob?.cancel()
        super.onCleared()
    }
}
