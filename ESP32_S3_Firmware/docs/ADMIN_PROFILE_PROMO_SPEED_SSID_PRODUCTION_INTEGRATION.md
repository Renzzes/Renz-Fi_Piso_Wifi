# Admin Profile / Promo Speed / SSID — Production Integration

## 1. Forensic findings

### Available Profiles
- `GET /api/router/profiles` is **cache-only** (`RouterPlatform::listProfiles` → `RouterCacheManager::fillProfiles`).
- Live `/ip/hotspot/user/profile/print` already existed on Test/Sync and on `MikroTikDriver::listProfiles`, but Admin GET never called live RouterOS.
- Blank router password already meant **keep stored** (`toRouterSavePayload`, `saveSettings`, `mergeSettings`).
- Save hotspot settings is local SD only — Test not required.
- Gap: cache miss had no one-shot profile refresh; owners had to Test or Synchronize.

### Promo Speed
- Promo stored `speed` (integer Mbps) but **runtime ignored it**.
- Coin → `minutesForAmount` → session time only; `HotspotUser.profile` left empty → MikroTik default profile from router settings.
- No profile dropdown; no managed `renzfi-speed-*` user profiles.

### Wireless SSID
- `PUT /api/router/wireless` already enqueued `AdminSaveWireless` → set SSID on configured interface.
- Gaps: response hard-coded `security=wpa2-psk`; `updateInterface` also called `ensureOpenSecurityProfile` (forces Open on non-open); wifiwave2 path cleared passphrase; verify only copied band.

### Profile rate-limit edit
- Display-only. No set API.

## 2. What already worked
- Cached profiles after Sync/Test.
- Optional password keep-on-blank.
- Async admin jobs (test/save/wireless/sync).
- Hotspot user create/update with `=profile=` when `HotspotUser.profile` set.
- SSID editable UI field + worker path.

## 3. Confirmed gaps (fixed)
1. Profile cache-miss auto-refresh (one worker job).
2. Promo ↔ MikroTik user profile association + runtime assign.
3. Custom speed → idempotent managed user profile.
4. Profile rate-limit edit (set + same-session verify + cache patch).
5. SSID-only mutation + same-session SSID verify + preserve security.

## 4. Changes required
See sections below / CHANGES APPLIED in the delivery report.

## 5. Profile architecture
```
/ip/hotspot/user/profile   (customer time/rate profiles)
        ↓
Test / Sync / AdminUserProfileOp refresh
        ↓
RouterCacheManager (profiles[], profileDetails[{name,rateLimit}])
        ↓
GET /api/router/profiles
        ↓
System Configuration + Promo Rates
```

Default Profile selection (settings `profile`) is independent of promo speed profiles but uses the same inventory.

## 6. Custom speed architecture
```
Promo custom Download/Upload Mbps
  → POST /api/router/profiles/op {action:ensure-managed,downloadMbps,uploadMbps}
  → router_worker AdminUserProfileOp
  → renzfi-speed-<down>m-<up>m  comment=RENZFI:managed-speed
  → Promo.managedProfileName
  → donePaying → session.hotspotProfile
  → HotspotUser.profile → /ip/hotspot/user add|set =profile=
```

## 7. RouterOS rate-limit semantics
Hotspot **user profile** `rate-limit` form used by this project:

```
rx/tx = client download / client upload
example: 10M/5M  → 10 Mbps down, 5 Mbps up
```

## 8. Managed-profile naming / idempotency
- Name: `renzfi-speed-{download}m-{upload}m`
- Ownership: comment starts with `RENZFI:`
- Exists + owned + same rate → unchanged
- Exists + owned + different rate → set
- Exists + not owned → error (no `-2`/`-3` proliferation)

## 9. SSID mutation architecture
```
PUT /api/router/wireless {ssid}
  → AdminSaveWireless
  → /interface/wireless/set .id=… ssid=…   (canonical interface)
  → same-session readInterface verify
  → canonical + RouterCacheManager.applyWirelessFields
```
No reboot. No interface disable/enable. Security-profile untouched on SSID-only.

Expected: customer Wi‑Fi clients must reconnect to the new SSID. Admin remains on ESP32 Ethernet (`10.10.10.2`).

## 10. Password optional behavior
- Blank password field → omit from save/test payload → keep NVS/SD secret.
- Profiles display from cache without re-entering password.
- Cache-miss refresh uses stored credentials via worker.

## 11. Cache behavior after mutation
- Profile ops: targeted verify → local cache upsert (no full Synchronize).
- Wireless: `applyWirelessFields` from verified result.
- FE: `refreshProductionRouterViews()` after jobs.

## 12. RouterOS command budget
| Scenario | Sessions | Commands (approx) |
|----------|----------|-------------------|
| System Config open, cache hit | 0 | 0 |
| Profile cache miss | 1 | 1× user/profile/print |
| Promo Rates open, cache hit | 0 | 0 |
| Custom speed save | 1 | print + add/set + verify |
| Rate-limit edit | 1 | print + set + print |
| SSID save | 1 | print + set + print |
| Idle nav | 0 | 0 |

## 13. CPU / stability protections
Preserved: single `router_worker`, session gate, reconnect cooldown, CPU pacing, bounded `.proplist`, no AsyncTCP RouterOS wait, no frontend RouterOS polling intervals.

## 14. API changes
- `POST /api/router/profiles/refresh` → `{action:refresh}` job
- `POST /api/router/profiles/op` → `{action, ...}` job  
  actions: `refresh` | `set-rate-limit` | `ensure-managed`
- Promo JSON optional fields: `speedMode`, `profileName`, `customDownloadMbps`, `customUploadMbps`, `managedProfileName`

## 15. Frontend changes
- System Configuration: one-shot refresh; Refresh Profiles → live refresh; Edit Rate Limit.
- Promo Rates: Speed select from `/api/router/profiles` + Custom Speed Limit.
- Wireless: unchanged UX; firmware preserves Open / verified SSID.

## 16. Backward compatibility
- Legacy promos without speed metadata → default router profile (unchanged).
- Legacy `speed` integer still stored/displayed when present.
- Existing cache schema unchanged (profiles + profileDetails).

## 17. Build results
Recorded in delivery report after `npm run build` / `pio run`.

## 18. Hardware validation procedure
Follow STATIC TESTS A–K and the hardware checklist in the product request. Do not claim PASS from static validation alone.

## 19. Unresolved observations
- Voucher activation path may not yet attach promo profiles (coin/done-paying path does).
- `devices` / `data_cap_mb` on promos remain storage-only.
- Dev Node `server/` stubs do not implement profile ops (embedded only).

## 20. REAL HARDWARE VALIDATION (post-integration flash)

### Boot / network (PASS)
- Firmware `0.5.0-w5500`, Provisioned, MikroTik driver
- Ethernet UP 100/full, IP `10.10.10.2`, GW/DNS `10.10.10.1`, MAC `42:1B:F6:FE:11:91`
- SPIFFS + SD mounted; Management AP OFF; Production Ready
- No Guru Meditation, TWDT, reboot, reconnect/queue storm, Ethernet drop

### Memory / DMA (PASS with observation)
- Idle heap ≈ 8.43 MB; DMA free≈81576 largest≈65524
- After Test: DMA free≈77816 largest≈65524 minimum≈39000
- No DMA allocation failure; minimum is historical low watermark, not proven leak

### Admin idle RouterOS (PASS)
- Page open: ESP-local APIs only (`/api/health`, `/api/status`, cache, profiles, …)
- **0 RouterOS sessions/commands** before Test — cache-first preserved

### RouterOS Test (PASS communication / cache populate)
| Item | Hardware |
|------|----------|
| Sessions | 1 (`admin-test`) |
| Identity | MikroTik (~1111 ms) |
| Resource | ~221 ms |
| Profile inventory | profiles=2 (~1300 ms) |
| SSID in cache | `Renz-Fi Piso Wifi` |
| Security in cache | **blank** (`security=`) — defect below |
| Elapsed | 4816 ms, http=200 |
| cache-not-populated | no (`ok=yes`) |

### Test latency accounting (EXPECTED PROTECTION — no change)
Approximate composition of ~4816 ms:
1. Session establish + login (TCP 8728)
2. identity ~1111 ms
3. resource ~221 ms
4. `/ip/hotspot/user/profile/print` ~1300 ms
5. `/ip/hotspot/print` (additional inventory)
6. Inter-command pacing (`ROUTER_CMD_DELAY_TIER1_MS=100` when CPU &lt;30%, higher tiers under load)
7. Possible connect cooldown residue (`ROUTER_API_MIN_CONNECT_INTERVAL_MS=5000`) only if a prior connect was recent
8. Worker/cache JSON persist (local)

No confirmed redundant second reconnect inside a single Test. Stability pacing retained. A 4–5 s manual Test is acceptable.

### Security blank — confirmed root cause (FIXED in follow-up)
**Expected:** `security=none` → UI **Open**  
**Actual hardware after Test:** `security=` (empty) → UI Unknown/—

**Exact failing layers:**
1. `MikroTikDriver::testSettings` never called `RouterWireless::readInterface` / security-profile print — only identity, resource, user profiles, hotspot server.
2. `RouterPlatform::test` applied canonical **SSID** into the snapshot but **did not copy** `out["security"]` (because it was never set).
3. Not: empty-auth-types misclassification (that path already maps success+empty → `none`).
4. Not: `copyStringField` rejecting `none` (`none` is non-empty).
5. Sync path (`collectCacheSnapshot`) already classified security correctly; Test path was incomplete.

**Fix:** same-session bounded wireless+security read inside `testSettings`; copy live `security`/`ssid`/`band`/`wirelessInterface` into cache snapshot. Failed security query → `unknown` (never Open).

### HTTP duplicate log lines (DOCUMENT ONLY)
- `/index.html` may log `WebServer/notFound` then `StaticFileServer` — diagnostic timer labels for the notFound→provider chain, not proven double body send.
- Frequent `/api/health` is ESP-local Admin/health monitoring — **not** RouterOS polling. Left untouched.

### Profile auto-load / Promo / Custom speed / SSID mutation
Hardware evidence above proves Test inventory + cache SSID. Full post-fix validation of:
- reboot → System Config without Test (cache-hit 0 RouterOS)
- cache-miss one-shot refresh
- Promo existing/custom profile runtime on MikroTik user
- SSID change preserving Open

remains **HARDWARE VALIDATION REQUIRED** after flashing this security follow-up.
