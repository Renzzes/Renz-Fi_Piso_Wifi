export type FirmwareUploadMeta = {
  filename?: string;
  sizeBytes?: number;
  mimeType?: string;
};

export function handleFirmwareUpload(_meta: FirmwareUploadMeta) {
  // Intentionally stubbed: ESP32 firmware/OTA remains handled externally.
  return {
    ok: true,
    message: "Firmware upload received (stub — no ESP32 OTA logic in admin server)",
  };
}
