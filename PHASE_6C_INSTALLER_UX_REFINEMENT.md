# Phase 6C — Installer-Friendly Network Setup

> **Status:** Implemented  
> **Date:** 2026-06-30  
> **Scope:** Setup Wizard UX only — zero firmware, API, or architecture changes.

---

## 1. Old Flow → New Flow

### Old Flow

```
Welcome
  ↓
Router Detection          ← technical screen: showed detection confidence, driver list,
  ↓                          vendor metadata
Driver Selection          ← technical screen: showed driver manifest, capabilities,
  ↓                          firmware version check, documentation URL
Router Connection
  ↓
Portal
  ↓
Coin
  ↓
Validation
  ↓
Summary
  ↓
Complete
```

**Problems with the old flow:**

- Installer saw terms like "Driver", "Manifest", "Capability flags", "Detection Confidence"
- Firmware version and model matching were exposed during early setup steps
- Two separate screens for what is conceptually one decision

---

### New Flow

```
Welcome
  ↓
Choose Network Type       ← installer-friendly: Standard Network or MikroTik Enhanced
  ↓                          (detection runs silently in background)
Router Setup
  ↓
Portal
  ↓
Coin
  ↓
Validation
  ↓
Summary
  ↓
Complete
```

**Improvements:**

- Single step for the entire network-mode decision
- Installer sees product language, not implementation language
- Detection result appears as a short contextual hint, not as a data table
- Step labels simplified: "Network Type", "Router Setup", "Portal", "Coin"

---

## 2. Network Type Screen

**Screen ID:** `router_detection` (firmware-defined, unchanged)  
**Component:** `src/pages/setup/screens/NetworkTypeScreen.tsx`

### Layout

```
Choose network type
──────────────────────────────────────────────────────
[If MikroTik detected]:
  ╔══════════════════════════════════════════════════╗
  ║ ✓ MikroTik router detected                       ║
  ║   MikroTik Enhanced has been pre-selected…       ║
  ╚══════════════════════════════════════════════════╝

┌──────────────────────────────────────────────────────┐
│  ◉  Standard Network         [ Recommended ]         │
│     Compatible with most installations               │
│                                                      │
│     Works with almost any Wi-Fi router, mesh         │
│     system, access point, or ISP router.             │
│                                                      │
│     ✓ Captive portal                                 │
│     ✓ Coin system                                    │
│     ✓ Vouchers & promos                              │
│     ✓ Sales reports                                  │
│     ✓ Multi-appliance management                     │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│  ○  MikroTik Enhanced                                │
│     For networks using a MikroTik RouterOS router    │
│                                                      │
│     Use this option if your network uses a           │
│     MikroTik RouterOS router.                        │
│                                                      │
│     ✓ Guided RouterOS setup                          │
│     ✓ Hotspot integration                            │
│     ✓ User synchronization                           │
│     ✓ Bandwidth profiles                             │
│     ✓ Router diagnostics                             │
└──────────────────────────────────────────────────────┘

Using TP-Link, Ruijie, OpenWRT, or another router?
Start with Standard Network — advanced driver options
are available in the router settings after setup.

                              [ Continue ]
```

### Copy Rules Applied

| ❌ Never show        | ✅ Use instead                        |
|---------------------|--------------------------------------|
| `generic_ap`        | Standard Network                     |
| `mikrotik`          | MikroTik Enhanced                    |
| Driver              | (not shown at all)                   |
| Manifest            | (not shown at all)                   |
| Capability flags    | (not shown at all)                   |
| Basic               | Recommended / Compatible             |
| Limited             | Compatible with most installations   |
| Reduced             | (not used)                           |

### Accessibility

- Cards use `role="radio"` / `aria-checked`
- Container uses `role="radiogroup"`
- Visual selection indicator uses `aria-hidden`

---

## 3. Automatic Recommendation Logic

Detection runs in the background as a non-blocking side-effect on component mount:

```
┌─────────────────────────────────────────────────────────────────────┐
│  Component mounts                                                   │
│       ↓                                                             │
│  provisioningClient.detectRouters()  ← existing API, unchanged      │
│       ↓                                                             │
│  Detection response received                                        │
│       ↓                                                             │
│  drivers[].driverId === "mikrotik"   ← internal check, not shown   │
│  AND (detected === true OR configured === true)?                    │
│       ↙ yes                  ↘ no                                  │
│  Pre-select "MikroTik Enhanced"   Pre-select "Standard Network"    │
│  Show: "MikroTik router detected" (no action, just hint)           │
│       ↓                                                             │
│  Detection failure → silent fallback to "Standard Network"         │
└─────────────────────────────────────────────────────────────────────┘
```

**Key behavior:**
- Detection never blocks the installer — the UI is interactive while detection runs
- If MikroTik is detected, it is **pre-selected with a notice**, but the installer can freely switch to Standard Network
- If detection fails (network error, timeout), Standard Network is silently selected — no error shown to the installer
- The installer is never required to understand *why* a recommendation was made

---

## 4. Standard vs MikroTik Comparison

| Feature                    | Standard Network       | MikroTik Enhanced       |
|----------------------------|------------------------|-------------------------|
| Router setup complexity    | IP address only        | IP + admin credentials  |
| Profile selection          | Not required           | Hotspot profile         |
| RouterOS integration       | No                     | Yes                     |
| Bandwidth profiles         | No                     | Yes                     |
| Router diagnostics         | No                     | Yes                     |
| Portal & Coin              | Yes                    | Yes                     |
| Vouchers & promos          | Yes                    | Yes                     |
| Works with any AP/router   | Yes                    | MikroTik only           |

**Installer copy rules:**

- Standard Network is **never** called Basic, Limited, or Reduced
- MikroTik Enhanced is **never** implied to be required
- Both options show the same set of Renz-Fi product features (portal, coins, vouchers)

---

## 5. Driver ID Mapping

The mapping is an internal implementation detail in `src/pages/setup/networkTypeOptions.ts`.
It is never surfaced in the installer UI.

```
Installer choice           Internal driverId
─────────────────────────────────────────────────────
Standard Network      →    generic_ap
MikroTik Enhanced     →    mikrotik
```

**API call made by `NetworkTypeScreen` on Continue:**

```typescript
provisioningClient.selectDriver({ driverId: "generic_ap" | "mikrotik" })
```

This is the same `POST /api/provisioning/select-driver` endpoint defined in the
frozen HTTP Route Contract. No new API. No new parameters.

---

## 6. Files Modified

### New files

| File | Purpose |
|------|---------|
| `src/pages/setup/networkTypeOptions.ts` | Friendly copy, driver ID constants, mode → ID mapping |
| `src/pages/setup/screens/NetworkTypeScreen.tsx` | New unified step (replaces detection + driver selection in the UI) |

### Modified files

| File | Change |
|------|--------|
| `src/pages/setup/screens/RouterConnectionScreen.tsx` | Branches on `routerDraft.selectedDriverId`: Standard form (gateway IP only) vs MikroTik form (full credentials) |
| `src/pages/setup/screens/DriverSelectionScreen.tsx` | Repurposed as a silent passthrough that immediately redirects to `router_connection` |
| `src/pages/setup/screens/index.tsx` | `router_detection` slot now renders `NetworkTypeScreen` instead of `RouterDetectionScreen` |
| `src/components/setup/stepRouter.ts` | Step labels only: "Router Detection" → "Network Type", "Driver Selection" → "Network Type", "Router Connection" → "Router Setup" |

### Unchanged files

| File | Reason kept |
|------|-------------|
| `src/pages/setup/screens/RouterDetectionScreen.tsx` | Kept on disk; no longer rendered in the installer flow |
| `src/pages/setup/routerManifestUtils.ts` | Kept on disk; no longer used in the installer flow |
| `src/pages/setup/routerDraft.ts` | Unchanged — `selectedDriverId` field already existed |
| All `src/contexts/ProvisioningContext.tsx` | Frozen — not modified |
| All `src/services/provisioning/` | Frozen — not modified |
| All firmware / `ESP32_S3_Firmware/` | Frozen — not modified |

---

## 7. Confirmation — Frozen Contracts

| Component | Status | Notes |
|-----------|--------|-------|
| Firmware (`ESP32_S3_Firmware/`) | ✅ Unchanged | Zero firmware changes |
| RouterPlatform / IRouterDriver | ✅ Unchanged | No firmware changes |
| GenericAPDriver / MikroTikDriver | ✅ Unchanged | Internal — not exposed to installer |
| ProvisioningEngine / ProvisioningServer | ✅ Unchanged | Firmware side unchanged |
| ProvisioningClient | ✅ Unchanged | Same API calls: `detectRouters()`, `selectDriver()`, `connectRouter()` |
| ProvisioningContext | ✅ Unchanged | Same hooks: `applyWorkflowData`, `setCurrentScreen`, `setLoading` |
| Installation Workflow | ✅ Unchanged | State machine is firmware-owned; all transitions are identical |
| HTTP Contracts | ✅ Unchanged | `POST /api/provisioning/select-driver`, `POST /api/provisioning/connect-router` unchanged |
| Device Profile Contract | ✅ Unchanged | Not touched |
| SETUP_SCREEN_ORDER | ✅ Unchanged | Screen IDs `router_detection`, `driver_selection` remain in the array |
| WORKFLOW_STEP_TO_SCREEN | ✅ Unchanged | Mapping from workflow steps to screen IDs unchanged |
| STATE_MAX_SCREEN | ✅ Unchanged | Navigation guards unchanged |
| SetupScreenProps contract | ✅ Unchanged | `NetworkTypeScreen` implements the same interface |
| Wizard Shell | ✅ Unchanged | No changes to `WizardShell`, `SetupCard`, or other shell components |
| Setup UI Kit | ✅ Unchanged | `SetupForm`, `SetupFormField`, `SetupInfoBanner`, `SetupStatusCard`, etc. — read-only |

---

## 8. Success Criteria Verification

> After implementation, a non-technical installer should be able to complete appliance
> setup without seeing RouterPlatform terminology, driver terminology, manifests, or
> capability metadata, while the underlying architecture continues using the existing
> driver abstraction internally.

**Verification:**

| Criteria | Result |
|----------|--------|
| Installer never sees "Driver" | ✅ Removed from all installer-facing screens |
| Installer never sees "Manifest" | ✅ Removed — `ManifestDetails` component no longer rendered |
| Installer never sees "Capability flags" | ✅ Removed — badge list no longer rendered |
| Installer never sees `generic_ap` | ✅ Mapped to "Standard Network" — ID is internal only |
| Installer never sees `mikrotik` | ✅ Mapped to "MikroTik Enhanced" — ID is internal only |
| Installer never sees "Detection Confidence" | ✅ Detection result is a plain prose hint only |
| Standard Network is never called Basic/Limited | ✅ Copy uses "Recommended" and "Compatible with most installations" |
| MikroTik is never implied as required | ✅ Installer is shown the recommendation but always has the option to choose Standard Network |
| RouterPlatform unchanged | ✅ Verified |
| Provisioning unchanged | ✅ Verified |
| REST APIs unchanged | ✅ Verified |
| Installation workflow unchanged | ✅ Verified |
| Frozen contracts unchanged | ✅ Verified |
