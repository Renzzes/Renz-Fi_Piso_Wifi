package com.renzfi.owner.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.data.repository.DeviceRepository
import com.renzfi.owner.model.NearbyApplianceEvaluation
import com.renzfi.owner.model.NearbyApplianceInfo
import com.renzfi.owner.model.StartupDiscovery
import com.renzfi.owner.model.VendoDevice
import com.renzfi.owner.util.ManagementApNetworkUtils
import com.renzfi.owner.util.NetworkUtils
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Result of the Mode A / Mode B startup check performed on the splash screen. */
sealed class StartupRoutingResult {
    /** Check has not yet been requested. */
    data object Idle : StartupRoutingResult()
    /** Probe is in progress — splash stays visible. */
    data object Checking : StartupRoutingResult()
    /** One or more devices are registered — navigate to My Vendo. */
    data object HasRegisteredDevices : StartupRoutingResult()
    /**
     * No registered devices and no reachable appliance (or a factory/found appliance was
     * detected — see [MainViewModel.performStartupCheck]). Always lands on the Appliance
     * Not Found screen; any nearby-appliance follow-up is offered exclusively via the
     * non-blocking [StartupDiscovery] dialog overlay, never automatic navigation.
     */
    data object NoDevicesApplianceNotFound : StartupRoutingResult()
}

class MainViewModel(application: Application) : AndroidViewModel(application) {
    private val repository = DeviceRepository(application)

    private val discoveryDismissMutex = Mutex()
    private var discoveryDismissedUntilMs: Long = 0L

    private val _startupResult = MutableStateFlow<StartupRoutingResult>(StartupRoutingResult.Idle)
    val startupResult: StateFlow<StartupRoutingResult> = _startupResult.asStateFlow()

    private val _activeDevice = MutableStateFlow<VendoDevice?>(null)
    val activeDevice: StateFlow<VendoDevice?> = _activeDevice.asStateFlow()

    private val _startupDiscovery = MutableStateFlow<StartupDiscovery>(StartupDiscovery.None)
    val startupDiscovery: StateFlow<StartupDiscovery> = _startupDiscovery.asStateFlow()

    /**
     * Mode A / Mode B startup check — called once from the splash screen.
     *
     * Mode B: one or more appliances are already in the registry → navigate to My Vendo
     * immediately without any network call.
     *
     * Mode A: no registered devices.
     * - If no network is reachable at all (Wi-Fi off, no data) skip the probe and show
     *   Appliance Not Found immediately — avoids a 5-second OkHttp timeout.
     * - Otherwise probe the Management AP IP (192.168.4.1):
     *     • NewAppliance + needsSetup=true  → NoDevicesApplianceNotFound + non-blocking
     *       "Set Up Appliance" discovery dialog (factory unit — never auto-launches the
     *       wizard; the installer/owner must explicitly tap through, same as every other
     *       discovery dialog).
     *     • NewAppliance + needsSetup=false → NoDevicesApplianceNotFound + discovery dialog
     *       (already-configured appliance; owner must add it through the normal flow)
     *     • AlreadyRegistered              → HasRegisteredDevices (device IS in registry;
     *       treat as Mode B and surface discovery dialog)
     *     • null / timeout / error         → NoDevicesApplianceNotFound
     *
     * Every code path is exception-safe. The result is informational only; the NavGraph
     * decides where to navigate — never this function.
     */
    fun performStartupCheck() {
        if (_startupResult.value is StartupRoutingResult.Checking) return
        viewModelScope.launch {
            _startupResult.value = StartupRoutingResult.Checking
            try {
                repository.migrateIfNeeded()
                val devices = repository.getDevices()
                if (devices.isNotEmpty()) {
                    _startupResult.value = StartupRoutingResult.HasRegisteredDevices
                    return@launch
                }
                // Mode A — no registered devices.
                // Fast-fail when there is no active network; skip the OkHttp timeout entirely.
                if (!NetworkUtils.isNetworkAvailable(getApplication())) {
                    _startupResult.value = StartupRoutingResult.NoDevicesApplianceNotFound
                    return@launch
                }
                val nearby = try {
                    repository.evaluateNearbySetupAppliance(getApplication())
                } catch (_: Exception) {
                    null
                }
                when (nearby) {
                    is NearbyApplianceEvaluation.NewAppliance -> {
                        if (nearby.info.needsSetup) {
                            // Factory appliance detected. Never auto-launch the wizard —
                            // land on the normal "not found" screen and surface the same
                            // non-blocking "Set Up Appliance" dialog used by background
                            // discovery, so first-launch and already-running behavior match.
                            _startupDiscovery.value = StartupDiscovery.NewAppliance(nearby.info)
                            _startupResult.value = StartupRoutingResult.NoDevicesApplianceNotFound
                        } else {
                            // Already-configured appliance not in local registry.
                            // Navigate to "not found" and surface a non-blocking dialog.
                            _startupDiscovery.value =
                                StartupDiscovery.FoundConfiguredAppliance(nearby.info)
                            _startupResult.value = StartupRoutingResult.NoDevicesApplianceNotFound
                        }
                    }
                    is NearbyApplianceEvaluation.AlreadyRegistered -> {
                        // Device IS in local registry (edge-case after migration).
                        // Treat as Mode B and surface the standard nearby dialog.
                        _startupDiscovery.value = StartupDiscovery.AlreadyRegistered(
                            device = nearby.device,
                            info = nearby.info,
                        )
                        _startupResult.value = StartupRoutingResult.HasRegisteredDevices
                    }
                    null -> _startupResult.value = StartupRoutingResult.NoDevicesApplianceNotFound
                }
            } catch (_: Exception) {
                _startupResult.value = StartupRoutingResult.NoDevicesApplianceNotFound
            }
        }
    }

    /** Reset startup state so the splash screen can re-run the check (e.g. Retry). */
    fun resetStartupCheck() {
        _startupResult.value = StartupRoutingResult.Idle
    }

    /**
     * Optional background check for a setup-SSID appliance. Called after My Vendo
     * screen is already visible; never blocks navigation. Failures are silently ignored.
     */
    fun scheduleBackgroundDiscovery() {
        viewModelScope.launch {
            try {
                if (isDiscoveryDismissCooldownActive()) return@launch
                val ssid = ManagementApNetworkUtils.currentWifiSsid(getApplication())
                if (!ManagementApNetworkUtils.isSetupSsid(ssid)) return@launch
                when (val nearby = repository.evaluateNearbySetupAppliance(getApplication())) {
                    is NearbyApplianceEvaluation.NewAppliance -> {
                        _startupDiscovery.value = if (nearby.info.needsSetup) {
                            StartupDiscovery.NewAppliance(nearby.info)
                        } else {
                            StartupDiscovery.FoundConfiguredAppliance(nearby.info)
                        }
                    }
                    is NearbyApplianceEvaluation.AlreadyRegistered ->
                        _startupDiscovery.value = StartupDiscovery.AlreadyRegistered(
                            device = nearby.device,
                            info = nearby.info,
                        )
                    null -> Unit
                }
            } catch (_: Exception) {
                // Discovery is optional; failures must not affect the main screen.
            }
        }
    }

    fun dismissStartupDiscovery() {
        dismissStartupDiscoveryFor(cooldownMs = DISCOVERY_DISMISS_COOLDOWN_MS)
    }

    fun clearStartupDiscovery() {
        _startupDiscovery.value = StartupDiscovery.None
    }

    fun dismissStartupDiscoveryFor(cooldownMs: Long = DISCOVERY_DISMISS_COOLDOWN_MS) {
        viewModelScope.launch {
            discoveryDismissMutex.withLock {
                discoveryDismissedUntilMs = System.currentTimeMillis() + cooldownMs
            }
            clearStartupDiscovery()
        }
    }

    private suspend fun isDiscoveryDismissCooldownActive(): Boolean =
        discoveryDismissMutex.withLock {
            System.currentTimeMillis() < discoveryDismissedUntilMs
        }

    fun setStartupAlreadyRegistered(device: VendoDevice, info: NearbyApplianceInfo) {
        _startupDiscovery.value = StartupDiscovery.AlreadyRegistered(device = device, info = info)
    }

    fun setActiveDevice(device: VendoDevice) {
        _activeDevice.value = device
    }

    private companion object {
        const val DISCOVERY_DISMISS_COOLDOWN_MS = 30_000L
    }
}
