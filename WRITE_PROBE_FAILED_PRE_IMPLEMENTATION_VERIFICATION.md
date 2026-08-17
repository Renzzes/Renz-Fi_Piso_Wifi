# WRITE_PROBE_FAILED — Final Pre-Implementation Verification

Date: 2026-08-10  
Scope: Verification only — **no code changes**  
Prior forensic: `WRITE_PROBE_FAILED_ROOT_CAUSE_FORENSIC.md`  
Verdict: **SAFE TO IMPLEMENT** (Option A only — probe-local absolute path)

---

## Verdict

**SAFE TO IMPLEMENT**

| Criterion | Result |
|---|---|
| Root cause isolated | Yes — `joinPath("/temp", ".write_probe")` rejects relative leaf before any SD write |
| Implementation scope | Probe-local; one call site |
| Shared path API change required? | No (if Option A) |
| Regression risk to working subsystems | Minimal under Option A |
| Ambiguity remaining for WRITE_PROBE_FAILED? | None material |

Do **not** implement Options B/C as part of this fix: they would change currently-failing asset path composition behavior and expand blast radius beyond WRITE_PROBE_FAILED.

---

## Verification A — Every `StoragePaths::joinPath()` caller

Repository search of firmware sources: **two** call sites of `StoragePaths::joinPath`.

| # | Source file | Function | Arguments | 2nd parameter class | Notes |
|---|---|---|---|---|---|
| 1 | `StorageManager.cpp` | `StorageManager::joinSdPath` | `(dir, leaf, buf, 128)` | Caller-supplied | Public wrapper; **no callers** of `joinSdPath(` exist anywhere in `ESP32_S3_Firmware/src` |
| 2 | `StorageManager.cpp` | `StorageManager::probeSdWritable` | `("/temp", ".write_probe", probePath, 128)` | **Hidden relative filename** (dotfile leaf, no leading `/`) | **Only live relative-leaf use of `joinPath`** |

**Conclusion:** `.write_probe` is the only relative leaf passed to `joinPath` in production code. The other `joinPath` entry (`joinSdPath`) is unused.

---

## Verification B — Every `joinAssetPath()` caller

`joinAssetPath` is **file-private** (anonymous namespace in `StoragePaths.cpp`). Callers:

| Caller | Parameters (dir, filename) | Expected output | Used by |
|---|---|---|---|
| `joinPath` (non-empty leaf) | `(dir, leaf)` | `dir + "/" + leaf` | Probe / unused `joinSdPath` |
| `sdBannerPath` | `("/assets/banner", filename)` | `/assets/banner/<filename>` | Declared; contract helpers prefer fixed names |
| `sdMusicPath` | `("/assets/music", filename)` | `/assets/music/<filename>` | Same |
| `sdLogoPath` | `("/assets/logo", filename)` | … | Same |
| `sdBackgroundPath` | `("/assets/background", filename)` | … | Same |
| `sdAdsPath` | `("/assets/ads", filename)` | e.g. `/assets/ads/ad1.webp` | `AssetManager`, `AssetResolver` |
| `sdVideosPath` | `("/assets/videos", filename)` | e.g. `/assets/videos/ad1.mp4` | `AssetManager`, `AssetResolver` |
| `sdIconsPath` / `sdFontsPath` / `sdDownloadsPath` | assets subdirs + filename | corresponding paths | Header-exported; no other `.cpp` call sites found |
| `contractBannerCurrentPath` | `("/assets/banner", "current.webp")` | `/assets/banner/current.webp` | `AssetResolver` |
| `contractMusicCurrentPath` | `("/assets/music", "current.mp3")` | `/assets/music/current.mp3` | `AssetResolver` |
| `contractLogoCurrentPath` | `("/assets/logo", "current.webp")` | … | `AssetResolver` |
| `contractBackgroundCurrentPath` | `("/assets/background", "current.webp")` | … | `AssetResolver` |
| `contractFirmwareUpdatePath` | `("/firmware", "update.bin")` | `/firmware/update.bin` | Declared helper |

**Impact of changing leaf validation (Options B/C):**

| Area | Effect of allowing relative leaves |
|---|---|
| Portal / Assets | `AssetResolver::resolveContractSdPath` would start succeeding where it currently fails `joinAssetPath` and falls through to legacy/SPIFFS/bundled tiers — **behavioral change** |
| Ads / Videos | `sdAdsPath` / `sdVideosPath` with `adN.webp` / `adN.mp4` would start succeeding |
| Banner/Music primary writes | `AssetManager` already uses **full** `Contract*` constants for banner/music/logo/background — largely insulated |
| Downloads / Icons / Fonts | Helpers become usable if later called |
| History / Config / Sales / Sessions | Use absolute contract paths via `StorageManager` — **not** via `joinAssetPath` |
| Backups / Restore / Journal / Replay | Do **not** call `joinAssetPath` for core JSON/history paths |

**Conclusion:** Changing `joinAssetPath` / `joinPath` validation is **not** probe-isolated. It touches asset resolution contracts.

---

## Verification C — Design intent of `isValidSdPath()`

Evidence:

```223:228:ESP32_S3_Firmware/src/StoragePaths.cpp
bool isValidSdPath(const char *path) {
  if (!path || path[0] != '/') return false;
  if (strstr(path, "..") != nullptr) return false;
  if (strlen(path) >= 128) return false;
  return true;
}
```

Docs (`STORAGE_ARCHITECTURE.md` Path safety):

- “All paths must start with `/`.”
- “Use `isValidSdPath()` before SD operations.”

Header comment on asset helpers: `// Generic asset join (filename validated)` — implies filenames are validated, but implementation validates them with **full-path** rules.

**Proven intent of `isValidSdPath`:** **FULL PATHS** (absolute, leading `/`, no `..`, length &lt; 128).  
It was **not** designed as a leaf-filename validator; using it on `"current.webp"` / `".write_probe"` is a contract misuse.

---

## Verification D — Contract consistency

| API | Validates `dir` as full path? | Validates `leaf`/`filename`? | Result path validated as full path? | Consistent with `isValidSdPath` intent? |
|---|---|---|---|---|
| `isValidSdPath` | N/A | N/A | Input must be absolute | Yes — full paths |
| `joinPath` | Yes (`isValidSdPath(dir)`) | Via `joinAssetPath` → **requires leaf absolute** | Yes on success | **Violates own composition purpose** for relative leaves |
| `joinAssetPath` | Implicit via copy of `dir` | **`isValidSdPath(filename)`** — wrong for relative names | Yes on success | **Internal contract violation**: named “filename” but requires `/` prefix |

**Exact violation:** `joinAssetPath` line `if (!isValidSdPath(filename)) return false;` applies full-path rules to a parameter documented/used as a **filename**.

Additionally: if a caller passed an absolute leaf like `"/.write_probe"`, concatenation would yield `/temp//.write_probe` — further evidence the helper was not carefully designed for absolute leaves either.

---

## Verification E — Options A–D (evaluate only)

### Option A — Pass `"/temp/.write_probe"` directly in the probe

| | |
|---|---|
| **Pros** | Smallest diff; touches only `probeSdWritable`; uses `isValidSdPath` correctly on a full path; no shared helper behavior change; matches forensic root cause |
| **Cons** | Does not repair latent broken relative-leaf joins in asset helpers (out of scope) |
| **Regression risk** | **Minimal** — probe-only |
| **API compatibility** | Unchanged public APIs |
| **Storage compatibility** | Unchanged JSON/history/fallback contracts |
| **Maintainability** | Clear; optional: add `constexpr` probe path constant next to `Temp` |
| **Performance** | Negligible (avoids failed join) |

### Option B — Modify `joinPath` to allow relative leaves

| | |
|---|---|
| **Pros** | Fixes composition API properly; would also unblock unused `joinSdPath` |
| **Cons** | Still routes through `joinAssetPath`; effectively same as C unless carefully split |
| **Regression risk** | **Medium–High** if it enables asset join success paths |
| **API compatibility** | Semantic change to existing helper |
| **Storage compatibility** | Indirect asset resolution changes possible |
| **Maintainability** | Better long-term, wrong for *this* hotfix |
| **Performance** | Neutral |

### Option C — Modify `joinAssetPath` validation

| | |
|---|---|
| **Pros** | Fixes root misuse for all relative filenames (`current.webp`, `adN.*`, `.write_probe`) |
| **Cons** | Broadest blast radius; changes `AssetResolver` contract-tier success rate |
| **Regression risk** | **Highest** among options for a “probe-only” release |
| **API compatibility** | Behavioral change for every `sd*Path` / `contract*Path` |
| **Storage compatibility** | Portal/asset path selection may change |
| **Maintainability** | Correct eventual fix; **not** this stability patch |
| **Performance** | Neutral |

### Option D — Introduce `joinLeaf()` for probe only

| | |
|---|---|
| **Pros** | Isolates probe; avoids touching broken `joinAssetPath` |
| **Cons** | New API surface for one call; more than Option A; temptation to migrate assets later without full testing |
| **Regression risk** | Low if unused elsewhere |
| **API compatibility** | Additive |
| **Storage compatibility** | OK if probe-only |
| **Maintainability** | Extra indirection vs literal absolute path |
| **Performance** | Neutral |

### Recommendation

**Implement Option A only.**

Technical justification: WRITE_PROBE_FAILED is caused solely by feeding a relative leaf into a full-path validator. Substituting the already-known absolute path `"/temp/.write_probe"` (after `ensureSdDirectory(Temp)`) makes the probe reach real SD I/O without changing any shared path helper. Options B/C are real architecture cleanups but are **out of scope** for this stability fix and risk changing asset resolution. Option D is unnecessary given Option A.

---

## Verification F — Hidden assumptions (`filename` / path starts with `/`)

| Subsystem | Depends on leading `/` for **full paths**? | Depends on `joinAssetPath` relative leaf succeeding? |
|---|---|---|
| Config / Sales / Sessions / Voucher JSON | Yes — absolute `StoragePaths::Contract*` / runtime aliases | No |
| History / NdjsonLedger | Yes — `isValidSdPath` on full history paths | No |
| Backups / Restore / Journal | Absolute paths / BackupManager constants | No |
| Replay / Conflicts | Manifest + absolute SD paths | No |
| SPIFFS fallback | Absolute `/fallback/...` | No |
| Portal / Admin static | Absolute `/www`, `/assets`, SPIFFS prefixes | Contract resolve via `joinAssetPath` **currently fails**; falls back to legacy/SPIFFS/bundled or metadata full paths |
| AssetManager banner/music/logo/bg | Uses **precomposed** `Contract*` absolutes | Bypass join for primary sdPath |
| Ads/Videos | Attempt `sdAdsPath`/`sdVideosPath` with relative leaves | **Currently fail** join; return StorageError / skip contract tier |
| Probe | Intended `/temp/.write_probe` | Broken via relative leaf |

No evidence that production JSON/sales/history/backup/restore require relative leaves to start with `/`. The `/` rule is correctly applied to **full paths**. The probe (and asset join helpers) incorrectly apply it to **leaves**.

---

## Verification G — Probe path uniqueness

Repo search for `write_probe` / `.write_probe`:

- **Only** `StorageManager::probeSdWritable` in `StorageManager.cpp`
- No duplicate probe implementations in Admin UI, tools, or other firmware modules

Intended probe file: **`/temp/.write_probe` only**.

---

## Verification H — Recovery transition if probe succeeds

From `StorageManager::begin()` when mount OK and probe returns true:

1. `_sdWritable = true` · `setDiagnosticCause("OK")`
2. Restore journal recovery (unchanged gate)
3. **`ensureLayout()`** runs (directories + seed) — currently skipped when probe fails
4. If SPIFFS manifest dirty → `syncFallbackToSd()`
5. If SPIFFS mounted → `replayHistorySpools()`
6. `_usingFallback = !_sdWritable && _spiffsMounted` → **false**
7. Log: `Sales storage = SD`
8. `refreshRuntimeSnapshot()` → health toward **HEALTHY** if layout OK and no pending replay/conflicts/emergency thresholds

Same pattern on remount success (`onSdRecoverySucceeded`) and mid-run restore (`verifySdHealthy` when probe flips true): clear `_usingFallback`, sync, replay, emit `storage.changed`.

**Automatic activation after successful probe:** writable flag, SD-primary writes, fallback flag clear, sync/replay when dirty, owner health update. No separate manual “enable SD” step beyond existing boot/recovery code.

---

## Verification I — Fallback when probe passes

`writeJson`:

- If `_sdWritable` → write SD (+ `checkpointToSpiffs` LKG for eligible files) — **not** emergency fallback mode
- SPIFFS fallback write path used when SD write unavailable / not writable

So: **new durable writes prefer SD automatically** when probe passes. Migration of dirty emergency files is **`syncFallbackToSd` / history replay`**, already invoked on the success path when manifest/spools exist. Checkpoint writes to SPIFFS may continue as LKG — that is not `_usingFallback`.

---

## Verification J — Regression safety (Option A probe-only)

| Area | Changes under Option A? |
|---|---|
| RouterWorker / RouterOS / sync / polling | No |
| Coin / Voucher / PortalSession / Sales / History managers | No code paths; only benefit if SD becomes writable |
| Admin Dashboard / Portal UI contracts | No API change |
| SPIFFS fallback implementation | Unchanged; may become inactive when writable |
| Conflict detection / restore / backups / hot-plug / watch / notifications | Unchanged logic |
| CPU / DMA / Heap / RouterOS cmd count | No new tasks; probe may do real I/O (intended) |
| `joinPath` / `joinAssetPath` semantics | **Unchanged** |

---

## Verification K — Expected boot sequence after Option A

Source-backed expected sequence when media is truly writable:

```
SD.begin OK
→ [storage] Verifying write capability
→ (ensure /temp)
→ SD.open("/temp/.write_probe") … verify … remove
→ [storage] Verification passed
→ ensureLayout / layout ready
→ optional sync + history replay
→ Sales storage = SD
→ fallback inactive (_usingFallback=false)
→ Health=HEALTHY (if no pending replay/conflicts/quota warnings)
→ diagnosticCause=OK
```

If media is **actually** read-only, probe should then fail at a **real** I/O step with `WRITE_PROBE_FAILED` or `WRITE_VERIFICATION_FAILED` — which would be a genuine signal (unlike today’s false negative).

Caveats (honest):

- Pending restore journal failure still forces `RESTORE_BLOCKED` / non-writable regardless of probe.
- Pending conflicts/replay can leave health `DEGRADED` even when writable.
- “Journal on SD” means restore/backup journal paths on SD when mounted writable — not a separate probe flag.

---

## Recommended implementation (discussion only — do not implement here)

In `probeSdWritable` only:

1. Keep `ensureSdDirectory(StoragePaths::Temp)`.
2. Replace `joinPath(Temp, ".write_probe", …)` with a full-path constant, e.g. `"/temp/.write_probe"` (or `StoragePaths` constexpr composed once as a full path string).
3. Optionally `isValidSdPath(probePath)` on that absolute string.
4. Leave all existing flush/read-back/delete verification intact.
5. Do **not** modify `joinPath` / `joinAssetPath` / asset callers in the same change.

**Confidence that Option A resolves WRITE_PROBE_FAILED without other storage behavior changes: Very high (~95%).** Remaining ~5% is genuine media RO or restore-blocked paths, which are correct outcomes.

---

## Out-of-scope finding (do not fix in this patch)

`joinAssetPath` currently rejects all relative asset filenames (`current.webp`, `ad1.webp`, …). Production banner/music often bypass via `Contract*` absolutes; `AssetResolver` contract-tier joins are latent-broken and fall through. Fixing that belongs to a separate, tested asset-path change — **not** the WRITE_PROBE_FAILED hotfix.

---

## Success criteria result

**SAFE TO IMPLEMENT** — root cause isolated, Option A scope well-defined, regression risk minimal, no additional forensics required for the probe false-negative itself.
