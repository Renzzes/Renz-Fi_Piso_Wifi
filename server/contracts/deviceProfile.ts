import { z } from "zod";

/** Frozen capability flags — extend only; never rename. See DEVICE_PROFILE_CONTRACT.md */
export const deviceCapabilitiesSchema = z
  .object({
    coin: z.boolean(),
    voucher: z.boolean(),
    assetUpload: z.boolean(),
    router: z.string().nullable(),
    fleet: z.boolean(),
  })
  .passthrough();

export type DeviceCapabilities = z.infer<typeof deviceCapabilitiesSchema>;

/** Frozen DeviceProfile — canonical appliance identity from GET /api/health data.device */
export const deviceProfileSchema = z
  .object({
    deviceId: z.string().min(1),
    serialNumber: z.string(),
    friendlyName: z.string(),
    deviceName: z.string().optional(),
    firmwareVersion: z.string(),
    version: z.string().optional(),
    hardwareRevision: z.string(),
    macAddress: z.string(),
    ipAddress: z.string(),
    routerDriver: z.string().nullable(),
    online: z.boolean(),
    capabilities: deviceCapabilitiesSchema,
  })
  .passthrough();

export type DeviceProfile = z.infer<typeof deviceProfileSchema>;

export const healthDeviceEnvelopeSchema = z
  .object({
    ok: z.boolean(),
    deviceId: z.string().optional(),
    deviceName: z.string().optional(),
    version: z.string().optional(),
    device: deviceProfileSchema.optional(),
  })
  .passthrough();
