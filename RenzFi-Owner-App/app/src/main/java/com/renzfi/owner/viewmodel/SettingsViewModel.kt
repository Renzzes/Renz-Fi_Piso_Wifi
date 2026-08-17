package com.renzfi.owner.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.BuildConfig
import com.renzfi.owner.RenzFiManagerApp
import com.renzfi.owner.data.datastore.UpdatePreferences
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.update.UpdateState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class SettingsUiState(
    val devices: List<VendoDevice> = emptyList(),
    val installedVersion: String = BuildConfig.VERSION_NAME,
    val versionCode: Int = BuildConfig.VERSION_CODE,
    val updateChannel: String = "stable",
    val lastUpdateCheckAt: Long = 0L,
    val hasUpdateAvailable: Boolean = false,
    val isCheckingForUpdates: Boolean = false,
)

class SettingsViewModel(application: Application) : AndroidViewModel(application) {

    private val repository = DeviceRepository(application)
    private val updatePreferences = UpdatePreferences(application)
    private val updateManager = (application as RenzFiManagerApp).updateManager

    private val _uiState = MutableStateFlow(SettingsUiState())
    val uiState: StateFlow<SettingsUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch {
            repository.devices.collect { devices ->
                _uiState.update { it.copy(devices = devices) }
            }
        }
        viewModelScope.launch {
            updatePreferences.updateChannel.collect { channel ->
                _uiState.update { it.copy(updateChannel = channel) }
            }
        }
        viewModelScope.launch {
            updatePreferences.lastUpdateCheckAt.collect { ts ->
                _uiState.update { it.copy(lastUpdateCheckAt = ts) }
            }
        }
        viewModelScope.launch {
            updateManager.state.collect { state ->
                _uiState.update {
                    it.copy(
                        hasUpdateAvailable = state is UpdateState.Available,
                        isCheckingForUpdates = state is UpdateState.Checking,
                    )
                }
            }
        }
    }

    fun deleteDevice(id: String) {
        viewModelScope.launch {
            repository.deleteDevice(id)
        }
    }

    fun checkForUpdates() {
        viewModelScope.launch {
            updateManager.checkForUpdate(manual = true)
        }
    }
}
