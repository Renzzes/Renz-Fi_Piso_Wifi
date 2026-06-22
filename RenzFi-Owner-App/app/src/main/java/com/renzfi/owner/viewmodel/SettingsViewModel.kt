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

data class SettingsUiState(
    val devices: List<VendoDevice> = emptyList(),
)

class SettingsViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = DeviceRepository(application)

    private val _uiState = MutableStateFlow(SettingsUiState())
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch {
            repository.devices.collect { devices ->
                _uiState.value = SettingsUiState(devices = devices)
            }
        }
    }

    fun deleteDevice(id: String) {
        viewModelScope.launch {
            repository.deleteDevice(id)
        }
    }
}
