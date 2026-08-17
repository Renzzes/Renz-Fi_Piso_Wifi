# Phase 7A Implementation Report — Device Registry & Device Switching

## Summary

Phase 7A adds multi-device support entirely on the **client side**. Each ESP32 appliance remains autonomous. Clients maintain a local Device Registry, discover appliances via `/api/health`, and switch targets by changing the API base URL only.

## Deliverables

| Item | Status | Location |
|------|--------|----------|
| Device Profile in firmware health | Done | `ESP32_S3_Firmware/src/DeviceIdentity.*`, `ApiServer.cpp` |
| Dev simulator health profile | Done | `server/index.ts` |
| Browser device types | Done | `src/types/deviceProfile.ts` |
| Browser localStorage registry | Done | `src/services/deviceRegistry.ts` |
| Subnet discovery | Done | `src/services/deviceDiscovery.ts` |
| Runtime API base URL (fleet) | Done | `src/services/embeddedApi.ts` |
| DeviceRegistry React context | Done | `src/contexts/DeviceRegistryContext.tsx` |
| Device Manager page | Done | `src/pages/DeviceManagerPage.tsx` |
| Header device selector | Done | `src/components/DeviceSelector.tsx` |
| Admin layout + `/devices` route | Done | `AdminLayout.tsx`, `App.tsx` |
| Android registry identity fields | Done | `VendoDevice.kt`, `HealthResponse.kt` |
| Android subnet discovery | Done | `DeviceRepository.kt` |
| Android UI discover actions | Done | `DeviceListScreen.kt`, `DeviceFormScreen.kt` |
| Architecture doc | Done | `DEVICE_REGISTRY_ARCHITECTURE.md` |

## Architecture Decisions

1. **Registry keyed by `deviceId`**, not IP — DHCP-safe identity.
2. **ProvisioningClient untouched** — fleet mode changes only `getRuntimeApiBaseUrl()` / `apiUrl()`.
3. **Direct Mode default** — empty runtime base URL = same-origin embedded SPA.
4. **Device switch preserves navigation** — pathname unchanged; session re-bootstrap per appliance.
5. **CORS** — firmware already sends `Access-Control-Allow-Origin: *`; cross-origin fleet requires re-login per device (expected).

## Browser Fleet Flow

1. User opens **Devices** (`/devices`) or uses header selector.
2. **Discover** scans subnet → upserts registry from health `device` profile.
3. **Switch** sets `runtimeApiBaseUrl` → clears queries → re-checks auth.
4. All dashboard pages continue using existing API modules via `apiUrl()`.

## Mobile Enhancements

- Existing multi-device list/settings flow preserved.
- **Discover** (search icon) on device list scans subnet.
- Add-device form prioritizes **Discover on subnet** over manual IP.
- `applianceDeviceId` links registry entries to firmware identity.

## Verification

```bash
# Web admin build
npm run build

# Android (from RenzFi-Owner-App/)
./gradlew assembleDebug

# Firmware (from ESP32_S3_Firmware/)
pio run
```

## Success Criteria Checklist

- [x] One appliance works exactly as today (Direct Mode)
- [x] Multiple appliances can be discovered
- [x] Multiple appliances can be added
- [x] User can switch appliances instantly (browser)
- [x] Browser supports Direct Mode and Fleet Mode
- [x] Mobile app uses the same registry concept
- [x] No ESP32 communicates with another ESP32
- [x] Every appliance remains independently deployable
- [x] Existing customers require zero migration
- [x] Build succeeds (web + Android verified; firmware: `pio run`)

## Known Limitations

- Cross-origin fleet mode cannot share session cookies between appliances — re-login required on switch.
- Subnet scan is LAN-only and may be slow on large ranges (254 hosts, 16 concurrent probes).
- Friendly name edits in the registry are client-side until Phase 7B cloud sync (if ever added).
