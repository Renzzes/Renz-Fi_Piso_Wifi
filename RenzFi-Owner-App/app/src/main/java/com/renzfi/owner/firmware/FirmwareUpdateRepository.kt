package com.renzfi.owner.firmware

import com.renzfi.owner.model.VendoDevice

/**
 * Phase 2 extension point — NOT implemented in Phase 1.
 *
 * Future responsibility: download the firmware .bin from GitHub
 * (via [FirmwareCatalogClient]) and upload it to the selected ESP32
 * appliance via POST /api/system/firmware.
 *
 * The no-op implementation below is registered at startup so the
 * interface can be injected anywhere without compile-time breakage.
 * Replace with a real implementation when Phase 2 begins.
 */
interface FirmwareUpdateRepository {
    suspend fun uploadFirmware(
        device: VendoDevice,
        manifest: FirmwareReleaseManifest,
        onProgress: (Int) -> Unit,
    ): Result<Unit>
}

/** Placeholder — always returns failure with a clear message. */
class NoOpFirmwareUpdateRepository : FirmwareUpdateRepository {
    override suspend fun uploadFirmware(
        device: VendoDevice,
        manifest: FirmwareReleaseManifest,
        onProgress: (Int) -> Unit,
    ): Result<Unit> = Result.failure(
        NotImplementedError("Firmware update is not available in this version of Renz-Fi Manager.")
    )
}
