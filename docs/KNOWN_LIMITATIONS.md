# Known Limitations — Renz-Fi Piso WiFi

**Operational status for the current freeze:** see [RELEASE_v0.5.0_FULLY_OPERATIONAL.md](RELEASE_v0.5.0_FULLY_OPERATIONAL.md) and the root README. The current validated build is fully functional and operational.

The notes below remain as engineering design boundaries and older pre-validation lists. They must not be read as “the appliance is not working.”

---

## Router drivers

| Driver | Status |
|--------|--------|
| **MikroTik** | Production-ready — full RouterOS API, hotspot provisioning, setup wizard support |
| **TP-Link** | Foundation only — driver registers but protocol is not implemented (`FoundationRouterDriver` stub) |
| **Ruijie** | Foundation only — driver registers but protocol is not implemented |
| **OpenWRT** | Foundation only — driver registers but protocol is not implemented |

Setup wizard dev simulator exposes MikroTik only. Non-MikroTik drivers appear in firmware manifests but cannot complete a real installation today.

## Portal configuration

- Setup wizard portal fields (name, theme, feature toggles) are sent in the provisioning body for forward compatibility; firmware currently verifies bundled/custom banner and music only via `PortalConfigManager`.
- Captive portal HTML/JS (`login.html`, `renzfi-app.js`) uses static UI; behaviour toggles configured in the wizard are not yet applied to the guest portal without a future firmware/portal.json phase.
- Banner, music, and other media uploads are Admin Dashboard only — not available in the setup wizard.

## Validation

- Firmware returns HTTP **400** when validation fails; per-check details may not be available in the error envelope (global error message only on production firmware).
- Dev simulator returns HTTP **200** with full `checks[]` for easier UI testing.

## Cloud and fleet

- Cloud synchronization — **not implemented**
- Multi-device / fleet management — **not implemented**
- Remote management — **not implemented**
- OTA fleet rollout — **not implemented** (single-appliance firmware update exists in Admin)

## Diagnostics and reporting

- Diagnostics page — **not implemented** (ValidationResult model ready for reuse)
- Health page — **not implemented**
- PDF installation report — **not implemented** (`InstallationReport` model ready)
- Support bundle export — **not implemented**

## TypeScript / lint

- Full `tsc --noEmit` reports pre-existing issues in unused UI kit files (`carousel`, `chart`) and some admin hooks — not introduced by Phase 6D.
- ESLint may report CRLF line-ending warnings on Windows across legacy files; run `prettier --write` on touched paths before release if enforcing lint in CI.

## Session and SSE

- Setup wizard SSE reconnect uses existing 3s EventSource retry; no exponential backoff.
- `installation.completed` does not auto-redirect to dashboard — user confirms via **Go to dashboard** on Complete screen (intentional UX).

## Simulator vs firmware

- Local dev uses Express provisioning simulator (`server/services/provisioningSimulator.ts`); behaviour mirrors firmware shapes but is not identical to on-device `ProvisioningEngine`.

---

For hardware validation, treat **MikroTik + coin-enabled firmware builds** as the primary supported path.
