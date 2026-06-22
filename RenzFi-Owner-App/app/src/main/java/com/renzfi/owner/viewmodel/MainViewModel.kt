package com.renzfi.owner.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.model.ConnectionState
import com.renzfi.owner.model.VendoDevice
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class MainViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = DeviceRepository(application)

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Idle)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _activeDevice = MutableStateFlow<VendoDevice?>(null)
    val activeDevice: StateFlow<VendoDevice?> = _activeDevice.asStateFlow()

    fun startHealthCheck() {
        if (_connectionState.value is ConnectionState.Checking) return

        viewModelScope.launch {
            _connectionState.value = ConnectionState.Checking
            val result = repository.resolveStartup(getApplication())
            _connectionState.value = result
            if (result is ConnectionState.Connected) {
                _activeDevice.value = result.device
            }
        }
    }

    fun checkAndOpenDevice(deviceId: String, onResult: (ConnectionState) -> Unit) {
        viewModelScope.launch {
            _connectionState.value = ConnectionState.Checking
            val device = repository.getDevice(deviceId)
            if (device == null) {
                val failed = ConnectionState.Failed(
                    message = "Device not found.",
                    reason = com.renzfi.owner.model.FailureReason.UNKNOWN,
                )
                _connectionState.value = failed
                onResult(failed)
                return@launch
            }
            val result = repository.checkDevice(getApplication(), device)
            _connectionState.value = result
            if (result is ConnectionState.Connected) {
                _activeDevice.value = result.device
            }
            onResult(result)
        }
    }

    fun setActiveDevice(device: VendoDevice) {
        _activeDevice.value = device
    }

    fun retry() {
        startHealthCheck()
    }
}
