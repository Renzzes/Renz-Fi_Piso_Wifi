package com.renzfi.owner.update

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.security.MessageDigest

/**
 * Handles APK SHA-256 verification and Android package installation.
 *
 * Uses [FileProvider] to construct a content:// URI and fires
 * ACTION_VIEW with the package MIME type so the system installer handles
 * the actual installation. No root or shell required.
 *
 * Security guarantee: [verifyApk] MUST return true before [installApk]
 * is called. The [UpdateManager] enforces this sequence.
 */
class ApkInstaller(private val context: Context) {

    fun canInstallPackages(): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.packageManager.canRequestPackageInstalls()
        } else {
            true
        }

    /**
     * Opens the system "Install unknown apps" settings page for this app so
     * the user can grant the permission. The caller should then re-attempt
     * installation.
     */
    fun openInstallPermissionSettings(context: Context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val intent = Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES).apply {
                data = Uri.parse("package:${context.packageName}")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        }
    }

    /**
     * Launches the Android system package installer for [apkFile].
     * If the install-unknown-apps permission has not been granted, opens
     * the settings page instead — the user must re-tap "Install" afterward.
     */
    fun installApk(context: Context, apkFile: File) {
        if (!canInstallPackages()) {
            openInstallPermissionSettings(context)
            return
        }

        val uri: Uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.fileprovider",
            apkFile,
        )

        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(intent)
    }

    /** Computes the lowercase hex SHA-256 digest of [file]. */
    suspend fun computeSha256(file: File): String = withContext(Dispatchers.IO) {
        val digest = MessageDigest.getInstance("SHA-256")
        file.inputStream().use { input ->
            val buffer = ByteArray(8 * 1024)
            var read: Int
            while (input.read(buffer).also { read = it } != -1) {
                digest.update(buffer, 0, read)
            }
        }
        digest.digest().joinToString("") { "%02x".format(it) }
    }

    /**
     * Returns true only if the file's SHA-256 matches [expectedSha256].
     * Comparison is case-insensitive hex.
     * Never install an APK that returns false here.
     */
    suspend fun verifyApk(file: File, expectedSha256: String): Boolean {
        val actual = computeSha256(file)
        return actual.equals(expectedSha256.trim().lowercase(), ignoreCase = true)
    }
}
