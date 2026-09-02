# Active Users Controls & Portal Banner (Aug 2026)

## Admin UI

### Sidebar
- **Disconnect** renamed to **Log out** (signs out of Admin only).

### Active Users actions

| Action | API | Behavior |
|--------|-----|----------|
| **Disconnect** | `POST /api/users/disconnect` | Pauses countdown and removes HotSpot internet. Session stays in Active Users. |
| **Connect** | `POST /api/users/reconnect` | Restores internet without adding time. Countdown continues from remaining balance. |
| **Terminate** | `POST /api/users/terminate` | Zeros time, removes internet, completes accounting. Portal shows owner notice on reload. |
| Pause / Resume | existing | Coin sessions only (customer pause budget unchanged). |

### Voucher timer freeze (forensic)

Voucher entitlement normally follows wall-clock `serviceExpiresAt`, so admin disconnect
previously dropped internet but time kept counting.

**Fix:** `suspendInternet()` sets `voucherClockFrozen` and freezes `secondsLeft`.
`enrichSessionCapabilities()` skips wall-clock override while frozen or paused.
`reconnectInternet()` rebuilds `serviceExpiresAt` from frozen remaining seconds.

### Owner terminate notice

`ownerTerminateSession()` sets `ownerDisconnectNotice` and `sessionNotice` on the
portal session payload. Captive portal (`renzfi-app.js`) displays the message when the
customer reopens the page:

> You have been disconnected by Owner, if this is a mistake, contact the owner

## Captive Portal banner

### Admin preview
- Full-width preview (`object-cover`, up to 192px height) instead of tiny `object-contain` box.
- Supports MP4 video preview when uploaded.

### Persistence / SD removed
- Custom banners were SD-only; SPIFFS copy was **removed** after SD write.
- **Fix:** `StorageManager::mirrorSdFileToSpiffs()` copies banner/music to
  `/portal/custom/banner.webp` after every successful SD save.
- `AssetResolver` serves SPIFFS tier when SD is absent — custom banner survives SD removal.

### Limits & formats
- Max banner size: **4 MB** (`PORTAL_BANNER_MAX_BYTES`).
- Allowed: PNG, JPEG, MP4 (short video banner on portal via `<video>` element).
- Default banner still used only after **Remove custom banner** in Admin.

### Portal reload showing default (forensic)

Causes:
1. SPIFFS mirror missing (fixed above).
2. `banner.onerror` in `renzfi-app.js` fell back to default even when `hasCustomBanner` was true.
3. `login.html` / `status.html` painted `Default-Banner.png` before async branding loaded.

**Fix:** onerror fallback only when custom banner is not configured.

### No default flash + single banner media (Aug 2026)

When a custom banner is configured, the portal must not briefly show the default image,
and must not show an empty video box under an image banner.

**Behavior:**
- Hero starts hidden (`#portalHero.branding-pending`) until branding resolves.
- `loadBranding()` runs **before** session fetch and other init work.
- Only one media element is shown: `<img>` for PNG/JPEG, `<video>` for MP4.
- Both elements use `display: none` until `.banner-active` is applied after preload.
- Default `Default-Banner.png` is used only when no custom banner is configured.

**API:** `/api/portal/branding` includes `bannerIsVideo` (mime + `.mp4` path).

**Deploy:** `npm run build:mikrotik-portal` (MikroTik files) and firmware build for ESP32 SPIFFS.

## Files changed

| Area | Files |
|------|--------|
| Session control | `PortalSessionManager.cpp/.h`, `ApiServer.cpp` |
| Banner storage | `AssetManager.cpp`, `StorageManager.cpp/.h`, `Config.h`, `PortalConfigManager.cpp` |
| Portal | `portal/login.html`, `portal/status.html`, `portal/renzfi-style.css`, `portal/renzfi-app.js` |
| Admin UI | `AdminLayout.tsx`, `ActiveUsersPage.tsx`, `CaptivePortalPage.tsx`, `users.ts`, `useActiveUsers.ts` |

## Deploy

1. Flash firmware (PortalSessionManager + AssetManager + StorageManager changes).
2. Run `npm run build:esp32` (or deploy pipeline) so `portal/` and Admin UI reach SPIFFS.
3. Re-upload custom banner once if an older upload has no SPIFFS mirror yet.
