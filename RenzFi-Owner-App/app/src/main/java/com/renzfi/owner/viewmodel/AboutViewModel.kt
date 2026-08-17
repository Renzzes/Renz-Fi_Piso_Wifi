package com.renzfi.owner.viewmodel

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.renzfi.owner.BuildConfig
import com.renzfi.owner.RenzFiManagerApp
import com.renzfi.owner.data.datastore.UpdatePreferences
import com.renzfi.owner.update.ReleaseManifest
import com.renzfi.owner.update.UpdateState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.io.File

data class AboutUiState(
    val installedVersion: String = BuildConfig.VERSION_NAME,
    val versionCode: Int = BuildConfig.VERSION_CODE,
    val updateChannel: String = "stable",
    val lastUpdateCheckAt: Long = 0L,
    val updateState: UpdateState = UpdateState.Idle,
    val dismissedVersion: String = "",
) {
    val pendingManifest: ReleaseManifest?
        get() = (updateState as? UpdateState.Available)?.manifest

    /**
     * True only when an update is available AND the user has not already
     * dismissed this specific version. Prevents the dialog from reappearing
     * after "Later" is tapped.
     */
    val showUpdateDialog: Boolean
        get() {
            val manifest = pendingManifest ?: return false
            return manifest.version != dismissedVersion
        }
}

class AboutViewModel(application: Application) : AndroidViewModel(application) {

    private val updateManager = (application as RenzFiManagerApp).updateManager
    private val updatePreferences = UpdatePreferences(application)

    private val _uiState = MutableStateFlow(AboutUiState())
    val uiState: StateFlow<AboutUiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch {
            updateManager.state.collect { state ->
                _uiState.update { it.copy(updateState = state) }
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
            updatePreferences.dismissedUpdateVersion.collect { version ->
                _uiState.update { it.copy(dismissedVersion = version) }
            }
        }
    }

    fun checkForUpdates() {
        viewModelScope.launch {
            updateManager.checkForUpdate(manual = true)
        }
    }

    fun downloadUpdate() {
        val manifest = _uiState.value.pendingManifest ?: return
        updateManager.downloadUpdate(manifest)
    }

    fun cancelDownload() {
        updateManager.cancelDownload()
    }

    /**
     * Stores the dismissed version so the dialog never shows again for it.
     * The dialog reappears automatically when a newer version is published.
     */
    fun dismissUpdate() {
        val manifest = _uiState.value.pendingManifest ?: return
        updateManager.dismissUpdate(manifest.version)
    }

    /**
     * [context] must be the Activity context from LocalContext.current in
     * the Compose layer — never stored in the ViewModel.
     */
    fun installApk(context: Context, apkFile: File) {
        updateManager.installApk(context, apkFile)
    }

    fun setChannel(channel: String) {
        viewModelScope.launch {
            updatePreferences.setUpdateChannel(channel)
            // Clear any dismissed version when the channel changes so the
            // user sees updates for their new channel.
            updatePreferences.clearDismissedUpdateVersion()
            updateManager.resetToIdle()
        }
    }

    fun resetError() {
        updateManager.resetToIdle()
    }
}
