package com.renzfi.owner.firmware

/**
 * Phase 2 extension point — NOT implemented in Phase 1.
 *
 * Future responsibility: fetch firmware release manifests from GitHub
 * (tag prefix "firmware/v*") for a given ESP32 hardware profile, then
 * provide the manifest to [FirmwareUpdateRepository] for delivery.
 *
 * Do not add implementation logic here until Phase 2 is started.
 */
interface FirmwareCatalogClient {
    /**
     * @param hardwareProfile e.g. "esp32s3-n16r8-w5500"
     */
    suspend fun fetchLatestFirmware(hardwareProfile: String): Result<FirmwareReleaseManifest>
}

/**
 * Metadata attached to every firmware/vX.Y.Z GitHub Release.
 * Schema mirrors the manager version.json but adds hardware-profile
 * and manager compatibility range fields.
 */
data class FirmwareReleaseManifest(
    val version: String,
    val channel: String,
    val binUrl: String,
    val sha256: String,
    val publishedAt: String,
    val hardwareProfile: String,
    val minManagerVersion: String,
    val releaseNotes: String,
)
