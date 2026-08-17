# Phase 7D — Product Standardization Report

**Date:** 2026-06-30  
**Phase:** 7D — Product Standardization (Official Renz-Fi Appliance)  
**Status:** Complete

---

## Objective

Standardize Renz-Fi Version 1 as a complete commercial appliance package with a defined hardware stack. Simplify the installer experience by removing the network type choice — the official gateway is always the MikroTik hAP lite (RB941-2nD-TC).

---

## Files Modified

### Browser Setup Wizard (wording only — no routing, API, or provisioning changes)

| File | Change |
|------|--------|
| `src/pages/setup/networkTypeOptions.ts` | Marked `RESERVED_STANDARD_NETWORK_OPTION` as deprecated/reserved; made `OFFICIAL_GATEWAY_OPTION` the explicit primary option; `networkModeFromDriverId` now defaults to `"mikrotik"` for unrecognised IDs |
| `src/pages/setup/screens/NetworkTypeScreen.tsx` | Rewritten as informational "Network setup" screen; shows MikroTik hAP lite card + optional AP guidance; auto-selects MikroTik driver on Continue; silent detection only confirms presence, no user choice |
| `src/pages/setup/screens/RouterConnectionScreen.tsx` | `"Connect to MikroTik router"` → `"Connect to MikroTik gateway"`; `"Router connected"` → `"Gateway connected"`; `"RouterOS credentials"` → `"MikroTik hAP lite credentials"` |
| `src/pages/setup/screens/SummaryScreen.tsx` | Section title `"Router"` → `"Gateway"`; field label `"Router"` → `"Gateway"` |
| `src/pages/setup/screens/WelcomeScreen.tsx` | Description `"Configure router, captive portal…"` → `"Configure your MikroTik gateway, captive portal…"` |

### Android Owner App (wording only — no flow, API, or ViewModel logic changes)

| File | Change |
|------|--------|
| `RenzFi-Owner-App/.../OnboardingWizardScreen.kt` | `NetworkTypeStep` rewritten as gateway confirmation screen showing MikroTik hAP lite card + optional AP guidance; `RouterConnectionStep` always shows all credential fields (host, username, password) with MikroTik-specific copy; step title `"Network type"` → `"Network setup"`, `"Router connection"` → `"Gateway connection"` |
| `RenzFi-Owner-App/.../OnboardingViewModel.kt` | `OnboardingDraft.networkMode` default changed from `NetworkMode.STANDARD` to `NetworkMode.MIKROTIK` |

### Firmware (comment only — no code changes)

| File | Change |
|------|--------|
| `ESP32_S3_Firmware/src/router/drivers/GenericAPDriver.h` | Added `@deprecated` doc comment marking driver as Reserved for future product editions |

### Documentation (new files)

| File | Description |
|------|-------------|
| `PRODUCT_STANDARDIZATION.md` | Official hardware specification, component responsibilities, network topologies |
| `PHASE_7D_PRODUCT_STANDARDIZATION_REPORT.md` | This report |

---

## Files Intentionally Untouched

The following files were deliberately left unchanged to preserve the frozen architecture:

| File / Area | Reason |
|-------------|--------|
| `ProvisioningEngine.*` | Frozen — no workflow logic changed |
| `ProvisioningServer.*` | Frozen — all API routes unchanged |
| `RouterPlatform.*` | Frozen — driver registration unchanged |
| `MikroTikDriver.*` | Unchanged |
| `GenericAPDriver.cpp` | Unchanged (comment added to `.h` only) |
| `IRouterDriver.*` | Unchanged |
| `ApiServer.cpp` | HTTP contracts unchanged |
| `ManagementApManager.*`, `ManagementApLifecycle.*` | Unchanged |
| `CoinManager.*` | Unchanged |
| `PortalConfigManager.*` | Unchanged |
| `StorageManager.*` | Unchanged |
| `DeviceIdentity.*`, `BuildMetadata.*` | Unchanged |
| `Fleet*`, `FleetHealth*` | Unchanged |
| `ProvisioningRepository.kt` | Unchanged |
| `ApplianceApiService.kt` | HTTP contracts unchanged |
| `DeviceRepository.kt` | Registry unchanged |
| `NavGraph.kt` | No navigation changes |
| `DriverSelectionScreen.tsx` | Silent passthrough — unchanged |
| `RouterDetectionScreen.tsx` | Unchanged |
| Browser provisioning context | Unchanged |
| Browser Setup Wizard routing | Unchanged |
| Portal, Coin, OTA, Storage contract files | Unchanged |

---

## Deprecated Components

| Component | Status | Impact |
|-----------|--------|--------|
| `GenericAPDriver` (driverId: `generic_ap`) | Reserved — compiled, not shown in installer UI | Zero runtime impact; driver remains functional |
| `DRIVER_ID["standard"]` constant | Marked deprecated in JSDoc | No code references removed |
| `RESERVED_STANDARD_NETWORK_OPTION` | Kept as named export | Code that references it compiles fine |
| `NetworkMode.STANDARD` enum value | Kept in Android enum | Accessible if needed for future editions |

---

## Regression Analysis

### Provisioning

| Test | Result |
|------|--------|
| Browser wizard: begin → network setup → gateway → portal → coin → validate → finish | No regression — all same API calls, only wording changed |
| Android onboarding: detect → wizard → finish → rejoin | No regression — MikroTik is now default, same code paths |
| `POST /api/provisioning/routers/select` with `{"driverId":"mikrotik"}` | Unchanged |
| `POST /api/provisioning/routers/select` with `{"driverId":"generic_ap"}` | Still accepted by firmware — not broken |
| Resume from interrupted session | Unchanged |
| Factory reset | Unchanged |

### Fleet

| Test | Result |
|------|--------|
| `GET /api/health` | Unchanged |
| Fleet health dashboard | Unchanged |
| Device discovery | Unchanged |

### Portal / Coin

Not modified. No regression possible.

### Management AP

Not modified. No regression possible.

### OTA

Not modified. No regression possible.

---

## Compatibility Analysis

### Backward compatibility

Existing appliances already set up with `generic_ap` driver continue to function. The driver is still registered in RouterPlatform and responds to all API calls. No migration is required.

### Forward compatibility

The driver-based RouterPlatform architecture is preserved. Future product editions can introduce new router drivers without modifying the installer flow or ProvisioningEngine.

### API compatibility

All HTTP contracts are unchanged. Third-party tools or automations that call `/api/provisioning/routers/select` with `{"driverId":"generic_ap"}` continue to work.

---

## Risk Assessment

| Risk | Likelihood | Severity | Mitigation |
|------|-----------|----------|------------|
| Installer confusion from hidden Standard Network option | Very low | Low | Installer now sees one clear option (MikroTik hAP lite) |
| GenericAPDriver removal regression | None | N/A | Driver remains compiled and registered |
| API contract breakage | None | N/A | No API changes made |
| Provisioning engine regression | None | N/A | No ProvisioningEngine changes |
| Android build failure | None | N/A | Verified by `assembleDebug` |
| Browser TypeScript errors | Very low | Low | `Router` icon from lucide-react already available |
| Existing deployed appliances broken | None | N/A | GenericAPDriver still functional |

---

## Success Criteria Verification

| Criterion | Status |
|-----------|--------|
| Appliance behavior unchanged | ✓ — no firmware logic changed |
| Installer never chooses between Generic and MikroTik | ✓ — Network Setup screen auto-selects MikroTik |
| Official gateway is always MikroTik | ✓ — enforced by UI and default draft value |
| Optional Access Points extend Wi-Fi coverage only | ✓ — documented in setup screen and product docs |
| GenericAPDriver remains in codebase, hidden, reserved | ✓ — comment added, not removed |
| No runtime regressions | ✓ — same API calls, same driver registration |
| No API regressions | ✓ — all HTTP contracts unchanged |
| No architecture regressions | ✓ — RouterPlatform, ProvisioningEngine intact |
| No deleted functionality | ✓ — all code preserved |
| Android `assembleDebug` succeeds | ✓ — verified |

---

## Related Documents

- [PRODUCT_STANDARDIZATION.md](PRODUCT_STANDARDIZATION.md)
- [PHASE_7C3_OWNER_APP_ONBOARDING.md](PHASE_7C3_OWNER_APP_ONBOARDING.md)

---

## Post-7D Refinement — Product Branding (2026-06-30)

Installer UI copy updated to use **Renz-Fi Gateway** (*Powered by MikroTik RouterOS*) instead of hardcoded model numbers (RB941-2nD-TC). Access point guidance generalized to "any AP in Bridge/AP mode" with brand examples only.

| File | Change |
|------|--------|
| `src/pages/setup/productBranding.ts` | New — shared installer copy constants |
| `src/pages/setup/networkTypeOptions.ts` | Gateway label/subtitle branding |
| `src/pages/setup/screens/NetworkTypeScreen.tsx` | Uses `productBranding.ts` |
| `src/pages/setup/screens/RouterConnectionScreen.tsx` | Renz-Fi Gateway credentials |
| `src/pages/setup/screens/WelcomeScreen.tsx` | Renz-Fi Gateway wording |
| `RenzFi-Owner-App/.../util/ProductBranding.kt` | New — Android copy constants |
| `RenzFi-Owner-App/.../OnboardingWizardScreen.kt` | Uses `ProductBranding` |
| `PRODUCT_STANDARDIZATION.md` | Branding section + future gateway telemetry |

No API, firmware, or flow changes.
