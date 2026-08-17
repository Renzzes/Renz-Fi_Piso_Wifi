# MikroTik Hotspot `/admin` Clean URL — Forensic

**Date:** 2026-08-06  
**Mode:** FORENSIC ONLY — no functional changes  
**Hardware-confirmed:** `/admin.html` launcher PASS; bare `/admin` → 404

## Why `/login` works without `.html`

RouterOS Hotspot defines **servlet request types** (`/`, `/login`, `/status`, `/logout`) that map to fixed filenames (`login.html`, etc.). This is **not** generic extension stripping.

`/admin` is **not** a Hotspot servlet type, so `admin.html` is **not** auto-selected for `/admin`.

## Why `/admin.html` works

Custom Hotspot HTML is served as **literal filenames** under `html-directory` (`.html` / `.htm` / `.txt` per MikroTik docs). Hardware: `10.20.0.1/admin.html` → launcher → ESP32.

## Bare `/admin` → 404

No servlet mapping; no file named exactly `admin` proven; no documented alias `/admin` → `admin.html`.

## Options (do not implement yet)

| Option | Support | Notes |
|--------|---------|--------|
| A. File `hotspot/admin` (extensionless) | **HARDWARE VALIDATION REQUIRED** | Docs emphasize `.html`; may or may not serve |
| B. `hotspot/admin/index.html` | **HARDWARE VALIDATION REQUIRED** | Language subdirs are `/lv/login`→`lv/login.html`, not directory index |
| C. Native alias/redirect | **No safe Hotspot-native** | Avoid proxy/NAT/firewall/script |
| D. Keep `/admin.html` | **PASS today** | Slightly longer bookmark |
| E. Reverse proxy / NAT | Rejected | CPU / stability |

## Recommended next step

Hardware-test A, then B, with **static file only**. Prefer whichever serves `/admin` with **0** RouterOS API cmds. If neither works, keep D (`/admin.html`) — do not add proxy/NAT.
