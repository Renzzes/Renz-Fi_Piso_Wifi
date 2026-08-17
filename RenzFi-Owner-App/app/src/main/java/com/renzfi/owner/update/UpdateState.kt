package com.renzfi.owner.update

import java.io.File

sealed class UpdateState {
    data object Idle : UpdateState()
    data object Checking : UpdateState()
    data object UpToDate : UpdateState()
    data class Available(val manifest: ReleaseManifest) : UpdateState()
    data class Downloading(
        val progressPercent: Int,
        val bytesDownloaded: Long,
        val totalBytes: Long,
    ) : UpdateState()
    data object Verifying : UpdateState()
    data class ReadyToInstall(val apkFile: File) : UpdateState()
    data object Installing : UpdateState()
    data class Error(val message: String, val cause: Throwable? = null) : UpdateState()
}
