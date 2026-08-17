package com.renzfi.owner.update

import android.app.Application
import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import com.renzfi.owner.data.datastore.UpdatePreferences
import com.renzfi.owner.data.repository.VersionRepository
import com.renzfi.owner.util.Constants
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import java.io.File
import kotlin.coroutines.resume

/**
 * Application-scoped update orchestrator. Instantiated once on
 * [com.renzfi.owner.RenzFiManagerApp] and observed by [AboutViewModel]
 * and [com.renzfi.owner.viewmodel.SettingsViewModel].
 *
 * Flow:
 *   scheduleStartupCheck() → waitForNetwork → 7 s delay → checkForUpdate(auto)
 *   checkForUpdate(manual)  → Available | UpToDate | Error
 *   downloadUpdate()        → Downloading → Verifying → ReadyToInstall | Error
 *   installApk()            → Installing → (system installer takes over)
 *   dismissUpdate()         → persists dismissed version → Idle
 */
class UpdateManager(private val context: Application) {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    val githubClient = GithubReleaseClient(context)
    val updatePreferences = UpdatePreferences(context)
    val versionRepository = VersionRepository(githubClient)
    val apkInstaller = ApkInstaller(context)

    private val _state = MutableStateFlow<UpdateState>(UpdateState.Idle)
    val state: StateFlow<UpdateState> = _state.asStateFlow()

    private var downloadJob: Job? = null

    // ── Startup ──────────────────────────────────────────────────────────────

    /**
     * Called once from [com.renzfi.owner.RenzFiManagerApp.onCreate].
     * Waits for internet connectivity, then delays [Constants.UPDATE_CHECK_DELAY_MS]
     * before performing a silent background check so the app remains fully usable.
     */
    fun scheduleStartupCheck() {
        scope.launch {
            try {
                waitForNetwork()
                delay(Constants.UPDATE_CHECK_DELAY_MS)
                checkForUpdate(manual = false)
            } catch (_: Exception) {
                // Startup check is best-effort; never surface to the user
            }
        }
    }

    // ── Check ────────────────────────────────────────────────────────────────

    suspend fun checkForUpdate(manual: Boolean) {
        // Do not interrupt an in-progress check or download
        val current = _state.value
        if (current is UpdateState.Checking) return
        if (current is UpdateState.Downloading) return
        if (current is UpdateState.Verifying) return

        _state.value = UpdateState.Checking

        try {
            val channel = updatePreferences.getUpdateChannel()
            val result = versionRepository.fetchLatestRelease(channel)

            updatePreferences.setLastUpdateCheckAt(System.currentTimeMillis())

            result.fold(
                onSuccess = { manifest ->
                    _state.value = if (versionRepository.isUpdateAvailable(manifest)) {
                        UpdateState.Available(manifest)
                    } else {
                        UpdateState.UpToDate
                    }
                },
                onFailure = { e ->
                    _state.value = if (manual) {
                        UpdateState.Error(e.message ?: "Update check failed", e)
                    } else {
                        UpdateState.Idle
                    }
                },
            )
        } catch (e: Exception) {
            _state.value = if (manual) {
                UpdateState.Error(e.message ?: "Update check failed", e)
            } else {
                UpdateState.Idle
            }
        }
    }

    // ── Download ─────────────────────────────────────────────────────────────

    fun downloadUpdate(manifest: ReleaseManifest) {
        if (downloadJob?.isActive == true) return

        downloadJob = scope.launch {
            _state.value = UpdateState.Downloading(0, 0L, 0L)

            val updateDir = File(context.cacheDir, Constants.UPDATE_APK_DIR)
            updateDir.mkdirs()
            val apkFile = File(updateDir, "RenzFi-Manager-${manifest.version}.apk")

            val result = githubClient.downloadApk(
                url = manifest.apkUrl,
                destFile = apkFile,
            ) { pct, downloaded, total ->
                _state.value = UpdateState.Downloading(pct, downloaded, total)
            }

            result.fold(
                onSuccess = { file ->
                    _state.value = UpdateState.Verifying
                    val verified = apkInstaller.verifyApk(file, manifest.sha256)
                    if (verified) {
                        updatePreferences.setPendingApkPath(file.absolutePath)
                        _state.value = UpdateState.ReadyToInstall(file)
                    } else {
                        file.delete()
                        updatePreferences.clearPendingApkPath()
                        _state.value = UpdateState.Error(
                            "SHA-256 verification failed. The downloaded file may be corrupted."
                        )
                    }
                },
                onFailure = { e ->
                    apkFile.delete()
                    updatePreferences.clearPendingApkPath()
                    _state.value = UpdateState.Error(e.message ?: "Download failed", e)
                },
            )
        }
    }

    fun cancelDownload() {
        downloadJob?.cancel()
        downloadJob = null
        _state.value = UpdateState.Idle
        cleanupTempFiles()
    }

    // ── Install ──────────────────────────────────────────────────────────────

    /**
     * Fires the system install intent. The Activity context must come from
     * the Compose layer (LocalContext) — never store it here.
     */
    fun installApk(context: Context, apkFile: File) {
        _state.value = UpdateState.Installing
        apkInstaller.installApk(context, apkFile)
    }

    // ── Dismiss / Reset ──────────────────────────────────────────────────────

    fun dismissUpdate(version: String) {
        scope.launch {
            updatePreferences.setDismissedUpdateVersion(version)
        }
        _state.value = UpdateState.Idle
    }

    fun resetToIdle() {
        _state.value = UpdateState.Idle
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private fun cleanupTempFiles() {
        scope.launch {
            runCatching {
                File(context.cacheDir, Constants.UPDATE_APK_DIR)
                    .listFiles()
                    ?.filter { it.name.endsWith(".tmp") }
                    ?.forEach { it.delete() }
            }
        }
    }

    /**
     * Suspends until the device has an internet-capable network connection.
     * Returns immediately if one is already available.
     */
    private suspend fun waitForNetwork() {
        val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

        val caps = cm.activeNetwork?.let { cm.getNetworkCapabilities(it) }
        if (caps?.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) == true) return

        suspendCancellableCoroutine { cont ->
            val callback = object : ConnectivityManager.NetworkCallback() {
                override fun onCapabilitiesChanged(
                    network: Network,
                    networkCapabilities: NetworkCapabilities,
                ) {
                    if (networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) {
                        runCatching { cm.unregisterNetworkCallback(this) }
                        if (cont.isActive) cont.resume(Unit)
                    }
                }
            }

            val request = NetworkRequest.Builder()
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build()

            runCatching { cm.registerNetworkCallback(request, callback) }

            cont.invokeOnCancellation {
                runCatching { cm.unregisterNetworkCallback(callback) }
            }
        }
    }
}
