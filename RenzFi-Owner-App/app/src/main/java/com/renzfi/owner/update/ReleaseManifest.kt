package com.renzfi.owner.update

import com.google.gson.annotations.SerializedName

/**
 * Parsed representation of the version.json asset attached to every
 * manager-android/vX.Y.Z GitHub Release.
 *
 * Schema:
 * {
 *   "version":      "1.0.1",
 *   "channel":      "stable",
 *   "apkUrl":       "https://github.com/.../RenzFi-Manager-v1.0.1.apk",
 *   "sha256":       "lowercase hex",
 *   "publishedAt":  "2026-06-23T08:00:00Z",
 *   "releaseNotes": "Human-readable changelog"
 * }
 */
data class ReleaseManifest(
    @SerializedName("version") val version: String,
    @SerializedName("channel") val channel: String,
    @SerializedName("apkUrl") val apkUrl: String,
    @SerializedName("sha256") val sha256: String,
    @SerializedName("publishedAt") val publishedAt: String,
    @SerializedName("releaseNotes") val releaseNotes: String,
)
