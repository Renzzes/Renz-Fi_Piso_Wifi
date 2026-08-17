# Renz-Fi Captive Portal — Source-of-Truth Forensic

**Mode:** READ-ONLY architecture mapping  
**Date:** 2026-08-12  
**Scope:** No application behavior changes. No deletes, moves, renames, rebuilds, flashes, or uploads.

---

## 1. Workspace inventory

### Portal-like trees that contain customer UI assets

| Directory | Role (from scripts/docs, proven) |
|-----------|----------------------------------|
| `portal/` | **Canonical source** (`PORTAL_SOURCE_DIR = "portal"` in `scripts/esp32-staging-manifest.mjs`) |
| `deployment/mikrotik-hotspot/` | **Generated MikroTik deploy output** + hand-written README / RSC templates |
| `Final_Build_Portal/` | **Release/upload overlay copy** synchronized from deployment by the same build |
| `ESP32_S3_Firmware/data/portal/` | **ESP32 SPIFFS staged recovery copy** (gitignored; from `npm run build:esp32` / `stage:esp32`) |
| `Captive Portal/` | **Legacy** tree; self-declares deprecated; **not** on the build path |
| `dist/` | **Vite admin SPA build** — React owner dashboard, **not** captive portal HTML/JS |

### Named assets found (excluding `node_modules/`, `.pio/`)

| Asset | Locations found |
|-------|-----------------|
| `renzfi-app.js` | `portal/`, `deployment/mikrotik-hotspot/`, `Final_Build_Portal/`, `ESP32_S3_Firmware/data/portal/`, `Captive Portal/` |
| `renzfi-style.css` | same five trees |
| `login.html` | same five + `Captive Portal/xml/login.html` (RouterOS sample) |
| `md5.js` | same five (identical hash across production trees) |
| `Default-Banner.png` | same five (`Captive Portal/` **different** large legacy banner) |
| `favicon.ico` | `portal/`, `deployment/…`, `ESP32…/data/portal/`, `Captive Portal/` (legacy differs); not in `Final_Upload` list but present under deployment |
| `bg_music.mp3` | `portal/`, `deployment/…`, `Final_Build_Portal/`, `Captive Portal/` (identical audio hash) |
| `coin.mp3` / `success.mp3` | `portal/`, `deployment/…`, `Final_Build_Portal/` only (not under `Captive Portal/` inventory) |
| `rlogin.html` / `status.html` / `logout.html` | **`Captive Portal/` only** in this workspace (native Hotspot servlets — expected to live on the **router**, not in `portal/`) |

### Build / deploy scripts (relevant)

| Script | Purpose |
|--------|---------|
| `package.json` → `build:mikrotik-portal` | Captive portal MikroTik bundle |
| `scripts/build-mikrotik-portal.mjs` | `portal/` → `deployment/mikrotik-hotspot/` → `Final_Build_Portal/` |
| `scripts/esp32-staging-manifest.mjs` | Declares `PORTAL_SOURCE_DIR = "portal"` |
| `scripts/ensure-portal-sources.mjs` | Validates/generates missing favicon/banner under `portal/` |
| `scripts/stage-esp32-data.mjs` | `dist/` + `portal/` → `ESP32_S3_Firmware/data/` |
| `package.json` → `build` / `build:esp32` | Vite admin → stage SPIFFS |
| `scripts/test-portal-session-lifecycle.mjs` | Tests **`Final_Build_Portal/renzfi-app.js`** (generated artifact) |
| `deployment/mikrotik-hotspot/upload-hotspot-files.rsc` | Post-upload RouterOS walled-garden helper (not a file copier) |
| `deployment/mikrotik-hotspot/README.md` | Documents upload from this generated directory |

`dist/` has `index.html`, `assets/`, PWA files — **admin**, not `renzfi-app.js`.

---

## 2–3. Duplicate-file table and SHA256 comparison

Inventory taken 2026-08-12 (read-only). Short SHA = first 16 hex of SHA256.

### `renzfi-app.js`

| Path | Size | SHA256 (full) | vs `portal/` |
|------|------|---------------|--------------|
| `portal/renzfi-app.js` | 59531 | `89A8D8EA7392595326E90B85F59FC5340CCA1CD85A71D49D85F7D23C991C70B4` | **SOURCE** (placeholder `__RENZFI_APPLIANCE_BASE_URL__`) |
| `deployment/mikrotik-hotspot/renzfi-app.js` | 59519 | `3F295D62671EAA69BEC33DFFEEFBF960A9681F9018E1672AC24B109BA695F05A` | **GENERATED DIFFERENCE** — URL substituted to `http://10.10.10.2` |
| `Final_Build_Portal/renzfi-app.js` | 59519 | `3F295D62671EAA69…` (same as deployment) | **IDENTICAL** to deployment |
| `ESP32_S3_Firmware/data/portal/renzfi-app.js` | 58481 | `04E54276A94A53F7F415A23C345D37D6F301DF7035ED2AC36A3E4FA61A545F42` | **DIFFERENT / STALE** (older staged copy; still has placeholder) |
| `Captive Portal/renzfi-app.js` | 27725 | `171E6C0B761E83DA8FA75640C62DA70CC0CF1303F354F5E2A9CAB804189EE216` | **DIFFERENT / LEGACY** |

Proof: substituting the placeholder in `portal/renzfi-app.js` with `"http://10.10.10.2"` yields a byte-identical match to `Final_Build_Portal/renzfi-app.js`.

### `login.html`

| Path | Size | SHA256 | Class |
|------|------|--------|-------|
| `portal/login.html` | 8664 | `7CF8BDFC247BEA9B58A08DC93F1211DF495508238E46EB63C6D16CD12A840059` | SOURCE |
| `deployment/…/login.html` | 8664 | same | **IDENTICAL** copy |
| `Final_Build_Portal/login.html` | 8664 | same | **IDENTICAL** copy |
| `ESP32_S3_Firmware/data/portal/login.html` | 8842 | `6865748C5C1A89A9…` | **DIFFERENT / STALE** |
| `Captive Portal/login.html` | 6671 | `485DD83F048D824F…` | **DIFFERENT / LEGACY** |

### `renzfi-style.css`

| Path | Class |
|------|-------|
| `portal/`, `deployment/`, `Final_Build_Portal/`, `ESP32…/data/portal/` | **IDENTICAL** (`EF3DD648357AF4E5…`, 22747 bytes) |
| `Captive Portal/` | **DIFFERENT** (`856CDAEDBDF9A7E0…`, 19663 bytes) |

### `md5.js`

All five trees (where present under production paths + Captive): **IDENTICAL** (`D98CB21A6028917E…`, 7218 bytes).

### Media

| Asset | `portal` / `deployment` / `Final_Build` | `Captive Portal` |
|-------|----------------------------------------|------------------|
| `bg_music.mp3` | IDENTICAL (`1471BDF73072582E…`) | IDENTICAL to those |
| `coin.mp3` / `success.mp3` | IDENTICAL across portal/deploy/Final | missing in Captive inventory |
| `Default-Banner.png` | IDENTICAL small banner (`3682906ED93AE193…`, 7266 B) | **DIFFERENT** large legacy (`0C6C3216082F1C39…`, 566116 B) |
| `favicon.ico` | IDENTICAL (`E61AAEA295824AAD…`, 671 B) | **DIFFERENT** (`7418A0AD5EFBCA00…`, 903 B) |

### HTML script/href references (same-directory relative)

`portal/`, `Final_Build_Portal/`, `deployment/mikrotik-hotspot/`, and `ESP32_S3_Firmware/data/portal/` `login.html` all reference:

- `renzfi-style.css`, `md5.js`, `Default-Banner.png`, `bg_music.mp3`, `coin.mp3`, `success.mp3`, `renzfi-app.js`

`Captive Portal/login.html` references the same pattern but lacks `coin.mp3` / `success.mp3` sources in the sample checked.

---

## 4. Source vs generated classification

| Path | Classification |
|------|----------------|
| `portal/` | **SOURCE** (canonical) |
| `deployment/mikrotik-hotspot/*.{html,js,css,ico,png,mp3}` | **GENERATED** (gitignored per `.gitignore`) |
| `deployment/mikrotik-hotspot/README.md`, `upload-hotspot-files.rsc`, `MIGRATION_*.md` | **SOURCE** (hand-written templates; keep) |
| `Final_Build_Portal/` | **RELEASE / DEPLOYMENT overlay** (generated sync; currently untracked; owner upload README is hand-written) |
| `ESP32_S3_Firmware/data/` | **GENERATED / CACHE for SPIFFS** (gitignored) |
| `dist/` | **GENERATED** Vite admin (gitignored) |
| `Captive Portal/` | **LEGACY** (README: “Deprecated — do not edit”) |

---

## 5. Build-chain analysis

### Captive portal (customer Hotspot UI)

```
portal/                          ← edit here only
   │
   │  RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2
   │  npm run build:mikrotik-portal
   │  (scripts/build-mikrotik-portal.mjs)
   │
   ├─ copy verbatim: login.html, css, md5, favicon, banner, audio
   ├─ substitute RENZFI_APPLIANCE_BASE_URL in renzfi-app.js + admin.html
   │
   ▼
deployment/mikrotik-hotspot/     ← primary generated MikroTik bundle
   │
   │  same script copies FINAL_UPLOAD_FILES
   ▼
Final_Build_Portal/              ← owner-facing upload overlay (subset)
```

`RENZFI_APPLIANCE_BASE_URL` is **required** by the build script (env var). Injected by replacing:

```js
var RENZFI_APPLIANCE_BASE_URL = "__RENZFI_APPLIANCE_BASE_URL__";
```

in `portal/renzfi-app.js` (and placeholder tokens in `portal/admin.html`).

### Admin SPA (ESP32 owner UI — separate product)

```
src/ (React) + public/
   │  npm run build  (vite → EMBEDDED_BUILD)
   ▼
dist/
   │  + portal/ (required recovery assets)
   │  npm run stage:esp32 / build:esp32
   ▼
ESP32_S3_Firmware/data/
   ├── index.html, assets/, …     (admin)
   └── portal/                    (recovery captive subset; no large audio)
```

Production customer portal is **not** served from ESP32 as the Hotspot shell; ESP32 serves API + admin + recovery portal assets.

---

## 6. MikroTik deployment-chain analysis

Proven from `scripts/build-mikrotik-portal.mjs` + `deployment/mikrotik-hotspot/README.md` + `Final_Build_Portal/README.md`:

| Question | Answer |
|----------|--------|
| What is uploaded to MikroTik? | Overlay files: `login.html`, `renzfi-app.js`, `renzfi-style.css`, `md5.js`, banner, audio (+ optional `admin.html` in deployment tree) |
| From which directory? | Documented deploy dir: **`deployment/mikrotik-hotspot/`**; owner overlay twin: **`Final_Build_Portal/`** (same generated content for upload list) |
| Copied from `portal/`? | **Yes**, by build — not uploaded raw with placeholder |
| Copied from `dist/`? | **No** |
| Is deployment generated? | **Yes** (assets); README/RSC are not |
| Is Final_Build generated? | **Yes** (synced each MikroTik portal build) |
| Manually edit either? | **Forbidden by build header** — edit `portal/` then rebuild |

Router must **retain** native Hotspot servlets (`alogin.html`, `redirect.html`, `status.html`, etc.) — those are **not** produced by `portal/` and must not be deleted on the router when overlaying.

---

## 7. ESP32 relationship

| Tree | ESP32 role |
|------|------------|
| `portal/` | Source for SPIFFS **recovery** portal subset |
| `ESP32_S3_Firmware/data/portal/` | Staged SPIFFS image contents (gitignored) |
| `deployment/` / `Final_Build_Portal/` | **MikroTik Hotspot** runtime — not ESP32 flash payload |
| `dist/` | Staged as admin SPA on ESP32 |
| `Captive Portal/` | **None** in current scripts |

Observed drift (dangerous): `ESP32_S3_Firmware/data/portal/{login.html,renzfi-app.js}` hashes **do not** match current `portal/` — staging was not re-run after later portal edits. SPIFFS recovery UI can lag MikroTik production UI until `npm run build:esp32`.

---

## 8. Canonical source-of-truth candidate

**`portal/`**

Evidence:

1. `PORTAL_SOURCE_DIR = "portal"` in `scripts/esp32-staging-manifest.mjs` (“Canonical captive portal sources — edit only files in portal/”).
2. `build-mikrotik-portal.mjs` reads exclusively from that directory.
3. `Captive Portal/README.md` already points editors to `portal/`.
4. Placeholder-based appliance URL only makes sense in source; generated trees contain the substituted URL.
5. Not chosen by folder name alone — chosen by the only build graph that produces production Hotspot + ESP32 recovery.

Confidence: **95%** (remaining 5%: operational habit of uploading from `Final_Build_Portal/` vs `deployment/mikrotik-hotspot/` — both are generated twins after a successful build; live router contents are outside this repo).

---

## 9. Dangerous duplicate paths

Exact failure mode already seen in development:

```
Cursor edits portal/renzfi-app.js          (A)  ← correct
        ↓
tests load Final_Build_Portal/renzfi-app.js (B)  ← stale if no rebuild
        ↓
operator uploads Final_Build / deployment   (C)  ← stale if no rebuild
        ↓
MikroTik Hotspot serves C                    (D)
```

Additional traps:

| Trap | Mechanism |
|------|-----------|
| Edit `Captive Portal/` | Never enters build; looks “real”; **wrong tree** |
| Edit `Final_Build_Portal/` or `deployment/…` generated files | Overwritten/ignored next build; or diverges silently |
| Edit `ESP32_S3_Firmware/data/portal/` | Gitignored; lost on restage; not MikroTik production |
| Rebuild MikroTik but not `build:esp32` | Hotspot updated, SPIFFS recovery stale (current state) |
| Lifecycle tests | Assert against **Final_Build_Portal**, not `portal/` — green tests can mean “old artifact still works” |

---

## 10. Recommended final architecture (not executed)

```
portal/                          SOURCE — only place humans edit captive UI
   │
   ├─ build:mikrotik-portal ──► deployment/mikrotik-hotspot/   GENERATED
   │                                 │
   │                                 └──► Final_Build_Portal/  RELEASE COPY
   │                                          │
   │                                          └──► MikroTik hotspot/ overlay
   │
   └─ build:esp32 / stage:esp32 ──► ESP32_S3_Firmware/data/portal/  GENERATED recovery

src/ + public/ ──► vite build ──► dist/ ──► ESP32_S3_Firmware/data/   admin SPA

Captive Portal/                  LEGACY — do not edit; eventual archive/removal
```

---

## 11. Should eventually be generated

- `deployment/mikrotik-hotspot/{login.html,admin.html,renzfi-app.js,renzfi-style.css,md5.js,favicon.ico,Default-Banner.png,*.mp3}`
- `Final_Build_Portal/*` except possibly keeping README as hand-written (already is)
- `ESP32_S3_Firmware/data/**`
- `dist/**`

---

## 12. Should eventually be source-controlled

- `portal/**` (entire canonical captive portal)
- `scripts/build-mikrotik-portal.mjs`, `esp32-staging-manifest.mjs`, `ensure-portal-sources.mjs`, `stage-esp32-data.mjs`, `portal-resolver.mjs`
- `deployment/mikrotik-hotspot/README.md`, `upload-hotspot-files.rsc`, `MIGRATION_*.md`
- Admin app under `src/`, `public/`, `package.json`

**Note (workspace state):** as of this forensic, `portal/`, `Final_Build_Portal/`, and `Captive Portal/` appear as **untracked** (`??`) in `git status`. That is a repo hygiene risk independent of the build graph — source should be tracked; generated should stay ignored.

---

## 13. Safe candidates for removal (NOT approved — do not delete)

| Candidate | Why candidate | Blocker |
|-----------|---------------|---------|
| `Captive Portal/` | Legacy; README deprecated; different hashes; unused by build | Confirm no human still uploads from it; archive first |
| Stale junk e.g. `deployment/mikrotik-hotspot/New Text Document.txt` | Not part of build | Trivial cleanup later |
| Re-generated contents under `Final_Build_Portal/` if policy becomes “build on demand only” | Duplicate of deployment | Keep until upload SOP is single-path |

**SAFE-TO-DELETE today:** **NONE until manually approved.**

---

## 14. Must NOT be removed

- `portal/` (canonical source)
- `scripts/*` portal/ESP32 staging builders
- `deployment/mikrotik-hotspot/README.md`, `upload-hotspot-files.rsc`, migration docs
- Router-native Hotspot files **on the device** (`alogin.html`, `redirect.html`, `status.html`, …) — not even present as production source under `portal/`
- ESP32 firmware sources under `ESP32_S3_Firmware/src/`
- `.gitignore` rules that protect generated deploy assets

---

## 15. Exact cleanup plan — DO NOT EXECUTE

1. **Policy freeze:** “Edit only `portal/`. Never edit `Captive Portal/`, `Final_Build_Portal/`, or generated files under `deployment/mikrotik-hotspot/`.”
2. **Track source:** `git add portal/` (and decide policy for large mp3 binaries / LFS if needed).
3. **Ignore release copies:** add `Final_Build_Portal/*` except `README.md` to `.gitignore` (mirror deployment), **or** stop committing generated overlay if it was never meant to be tracked.
4. **Single upload SOP:** pick one of `deployment/mikrotik-hotspot/` **or** `Final_Build_Portal/` as the documented Winbox source; both must be produced by the same build command.
5. **Test alignment (future code change — not now):** point lifecycle tests at freshly built artifact or build as pretest.
6. **Archive `Captive Portal/`:** move to `archive/` or delete only after written approval and backup.
7. **Restage SPIFFS** when portal changes: `npm run build:esp32` so `data/portal` matches `portal/`.
8. **No automatic `rm`/`Remove-Item` in agent sessions** without explicit user approval.

---

## FINAL REQUIRED SUMMARY

```
CANONICAL PORTAL SOURCE:
portal/

BUILD COMMAND:
RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2 npm run build:mikrotik-portal

BUILD OUTPUT:
deployment/mikrotik-hotspot/  (primary generated bundle)
Final_Build_Portal/           (synchronized release/upload overlay from the same command)

MIKROTIK DEPLOYMENT SOURCE:
deployment/mikrotik-hotspot/  OR  Final_Build_Portal/  (byte-identical for upload list after a successful build; both generated — do not hand-edit)

ESP32 PORTAL SOURCE:
portal/  →  staged to ESP32_S3_Firmware/data/portal/ via npm run build:esp32 / stage:esp32
(production guest UI is MikroTik Hotspot, not ESP32; ESP32 holds recovery/admin)

GENERATED DIRECTORIES:
deployment/mikrotik-hotspot/ (generated assets)
Final_Build_Portal/ (generated overlay)
ESP32_S3_Firmware/data/
dist/

LEGACY DIRECTORIES:
Captive Portal/

SAFE-TO-DELETE:
NONE until manually approved

DO-NOT-DELETE:
portal/
scripts/build-mikrotik-portal.mjs
scripts/esp32-staging-manifest.mjs
scripts/ensure-portal-sources.mjs
scripts/stage-esp32-data.mjs
deployment/mikrotik-hotspot/README.md
deployment/mikrotik-hotspot/upload-hotspot-files.rsc
deployment/mikrotik-hotspot/MIGRATION_*.md
(router-native Hotspot servlets on the device)

SOURCE-OF-TRUTH CONFIDENCE:
95%
```
