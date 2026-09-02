# Admin Dashboard Forensic Fixes (Aug 2026)

Forensic review and fixes for User History, System Configuration status, storage
conflicts, sales inflation, and Admin UI polish.

## 1. User History — Profile and Speed

### What `Profile` means

The **Profile** column is the MikroTik HotSpot **user profile name** (RouterOS) applied
when the customer paid. It comes from promo resolution at Done Paying (coin) or from
the voucher record (voucher). It is **not** a Renz-Fi Admin role or operator account.

Source: `PortalSessionManager` → `SaleRecord.profile` → `/sales/sales.json` →
`GET /api/sales/records` → `ActiveUsersPage` User History table.

### Speed availed

**Speed** is the human-readable bandwidth label from the matched promo (for example
`10/10 Mbps` or `5 Mbps`). Voucher sales already stored `sale.speed` from the voucher
reservation. Coin sales did **not** populate speed until this fix.

**Fix:** `PromoManager::resolveSpeedLabelForAmount()` + `RecordSale` worker now sets
`sale.speed` from the best matching promo at Done Paying.

## 2. System Configuration — Internet “Not Configured”

### Root cause

The **Internet** row uses `wan.*` from the router cache (`observation.wan`), populated
only after RouterOS WAN observation. Sync and Refresh **intentionally skipped** WAN
probes (comment: Phase 3 diagnostics only). When `wan.known === false`, the UI showed
**Not Configured** — meaning **WAN never probed**, not “customer has no portal session”.

This is **not** captive portal session state. MikroTik can have working internet while
the Admin cache still shows “not probed” until Sync/Refresh runs.

### Fix (firmware)

- `MikroTikDriver::observeAndRepairWan(observation, allowRepair)` — repairs gated by
  `allowRepair` (default `true` for Test Connection).
- **Refresh Router Information** and **Synchronize Router** call
  `observeAndRepairWan(observation, false)` — read-only WAN observe + ICMP ping, no
  DHCP/route mutations.

### Fix (Admin UI)

- Label changed from **Not Configured** → **Not probed yet** when WAN unknown.
- System Configuration Status panel explains: run Sync or Refresh to probe ether1-WAN.

## 3. SD Card Health — SPIFFS/SD conflict on `portal_sessions.json`

### Forensic

By design, `StorageManager::syncFallbackToSd()` calls `recordConflict()` when SPIFFS
and SD copies diverge during SD remount — **no automatic merge** (owner review baseline).

The card can remain **HEALTHY** (mounted, writable) while `pendingConflicts > 0`. The
message *“SPIFFS/SD CONFLICT detected; owner review required”* is informational, not
proof the SD is broken.

Typical cause: SD was removed or remounted; SPIFFS held a fallback checkpoint of
`/sessions/portal_sessions.json` that differed from the SD generation.

### Fix

- When a **valid SD read** succeeds, `clearConflictForPath()` removes stale conflict
  entries for that path (SD is authoritative).
- `StorageHealthCard` already explains HEALTHY + conflicts; no card replacement needed.

## 4. Firmware version in System Configuration

`SystemBuildInfo` already existed but emphasized staged SPIFFS metadata. Updated to
show **Running Firmware** prominently (same source as Firmware Update:
`/api/health` → `device.firmwareVersion` / `version`).

## 5. Change Admin Password — current password field

Passwords are stored as **hashes only** (`AuthManager`). The appliance **cannot**
retrieve plaintext for pre-fill.

**UI fix:**

- Show/hide toggle on all password fields (`PasswordField`).
- System Settings seeds `defaultOldPassword="admin"` (factory default) as the starting
  value when unchanged.
- Helper text explains hash-only storage.

## 6. Support contact links (UI only)

`SupportContactLinks` on Login page and Admin sidebar footer:

- FB: [Rence Bersamora](https://www.facebook.com/rence.bersamora)
- Phone: 09624816474

No firmware or API changes.

## 7. Dashboard — Coin Slot + Coin State merged

Redundant **Coin Slot** and **Coin State** collapsible panels merged into one **Coin
Slot** panel with feature, hardware, state, enabled, totals, and last activity.

## 8. Sales inflation (₱2 inserted → ₱7 profit)

### Root causes (proven)

1. **Duplicate sale rows:** Each Done Paying `RecordSale` job minted a new `psale-*`
   id. `upsertSale` deduped by `sale.id` only, so the same `sessionId` could produce
   multiple coin sale rows.
2. **SSE UI inflation:** `useDashboardEvents` `applySaleCreatedPatch` bumped dashboard
   totals on every `sale.created` with no sale-id dedupe.

### Fix

- `SessionManager::upsertSale` removes existing coin sale rows with the same
  `sessionId` before insert.
- `applySaleCreatedPatch` dedupes by `sale.created` payload `id`.

### Note on existing inflated history

Duplicate rows already in `/sales/sales.json` are not auto-deleted. After firmware
flash, new Done Paying events will not duplicate. Owner may export backup and trim
history if needed.

## Files changed

| Area | Files |
|------|--------|
| Coin sale speed | `PromoManager.cpp/.h`, `PortalSessionManager.cpp` |
| Sales dedupe | `SessionManager.cpp`, `useDashboardEvents.ts` |
| WAN observe | `MikroTikDriver.cpp/.h` |
| SD conflict clear | `StorageManager.cpp/.h` |
| Admin UI | `ActiveUsersPage.tsx`, `SystemConfigurationPage.tsx`, `systemConfigurationStatus.ts`, `SystemBuildInfo.tsx`, `ChangeAdminPasswordForm.tsx`, `PasswordField.tsx`, `SupportContactLinks.tsx`, `AuthPage.tsx`, `AdminLayout.tsx`, `DashboardPage.tsx`, `SystemSettingsPage.tsx` |

## Verification

1. Flash firmware; run **Refresh Router Information** — Internet should reflect MikroTik WAN.
2. Insert coin, Done Paying — User History shows Profile + Speed.
3. Dashboard profit should match persisted sales (no SSE double-count on reconnect).
4. SD conflict on `portal_sessions.json` clears after valid SD read when SD wins.
