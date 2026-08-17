package com.renzfi.owner.update

import android.content.Context
import com.google.gson.Gson
import com.google.gson.annotations.SerializedName
import com.google.gson.reflect.TypeToken
import com.renzfi.owner.util.Constants
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.TimeUnit

/**
 * Dedicated OkHttp client for GitHub Releases API traffic.
 *
 * Intentionally separate from the LAN appliance Retrofit client so that
 * GitHub timeouts, headers, and caching never interfere with device
 * discovery or the admin WebView session.
 */
class GithubReleaseClient(private val context: Context) {

    private val client = OkHttpClient.Builder()
        .connectTimeout(15, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .writeTimeout(30, TimeUnit.SECONDS)
        .build()

    private val gson = Gson()

    /**
     * Fetches the latest release manifest for [channel] (stable | beta).
     * Walks the GitHub Releases list, filters tags that start with
     * [Constants.GITHUB_TAG_PREFIX], respects the prerelease flag for
     * stable, then downloads and parses the version.json asset.
     */
    suspend fun fetchLatestRelease(channel: String): Result<ReleaseManifest> =
        withContext(Dispatchers.IO) {
            runCatching {
                val url = "https://api.github.com/repos/${Constants.GITHUB_OWNER}" +
                    "/${Constants.GITHUB_REPO}/releases"

                val request = Request.Builder()
                    .url(url)
                    .header("Accept", "application/vnd.github+json")
                    .header("X-GitHub-Api-Version", "2022-11-28")
                    .build()

                val body = client.newCall(request).execute().use { response ->
                    if (!response.isSuccessful) {
                        error("GitHub API returned ${response.code}")
                    }
                    response.body?.string() ?: error("Empty response from GitHub")
                }

                val type = object : TypeToken<List<GithubRelease>>() {}.type
                val releases: List<GithubRelease> = gson.fromJson(body, type)

                val filtered = releases
                    .filter { it.tagName.startsWith(Constants.GITHUB_TAG_PREFIX) }
                    .filter { release ->
                        if (channel == Constants.UPDATE_CHANNEL_BETA) true
                        else !release.prerelease
                    }

                val latest = filtered.firstOrNull()
                    ?: error("No releases found for channel: $channel")

                val versionJsonAsset = latest.assets.find { it.name == "version.json" }
                    ?: error("version.json asset not found in release ${latest.tagName}")

                downloadVersionJson(versionJsonAsset.browserDownloadUrl)
            }
        }

    private fun downloadVersionJson(url: String): ReleaseManifest {
        val request = Request.Builder().url(url).build()
        val json = client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) error("Failed to fetch version.json: ${response.code}")
            response.body?.string() ?: error("Empty version.json")
        }
        return gson.fromJson(json, ReleaseManifest::class.java)
    }

    /**
     * Streams the APK from [url] into [destFile], reporting progress via
     * [onProgress](percentComplete, bytesDownloaded, totalBytes).
     * Writes to a .tmp file first; renames on completion for atomicity.
     */
    suspend fun downloadApk(
        url: String,
        destFile: File,
        onProgress: (Int, Long, Long) -> Unit,
    ): Result<File> = withContext(Dispatchers.IO) {
        runCatching {
            destFile.parentFile?.mkdirs()
            val tempFile = File(destFile.parent, "${destFile.name}.tmp")

            val request = Request.Builder().url(url).build()
            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) error("APK download failed: ${response.code}")
                val responseBody = response.body ?: error("Empty APK response body")
                val totalBytes = responseBody.contentLength()
                var downloaded = 0L

                FileOutputStream(tempFile).use { out ->
                    responseBody.byteStream().use { input ->
                        val buffer = ByteArray(8 * 1024)
                        var read: Int
                        while (input.read(buffer).also { read = it } != -1) {
                            out.write(buffer, 0, read)
                            downloaded += read
                            val pct = if (totalBytes > 0)
                                ((downloaded.toDouble() / totalBytes) * 100).toInt()
                            else -1
                            onProgress(pct, downloaded, totalBytes)
                        }
                    }
                }
            }

            if (!tempFile.renameTo(destFile)) {
                tempFile.copyTo(destFile, overwrite = true)
                tempFile.delete()
            }
            destFile
        }.onFailure {
            // Clean up partial downloads
            runCatching { File(destFile.parent, "${destFile.name}.tmp").delete() }
        }
    }

    // ── Internal GitHub API models ───────────────────────────────────────────

    private data class GithubRelease(
        @SerializedName("tag_name") val tagName: String,
        @SerializedName("prerelease") val prerelease: Boolean,
        @SerializedName("assets") val assets: List<GithubAsset>,
    )

    private data class GithubAsset(
        @SerializedName("name") val name: String,
        @SerializedName("browser_download_url") val browserDownloadUrl: String,
    )
}
