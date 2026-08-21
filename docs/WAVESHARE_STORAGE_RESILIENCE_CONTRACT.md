# Waveshare Storage Resilience Contract

**Status:** Preserves existing N16R8 / production storage hardening behavior  
**Branch:** `feature/waveshare-esp32-s3-eth`  
**Does not replace:** SD/DMA concurrency forensic track

## Principle

> Internal ESP32 storage is an **operational resilience layer**.  
> It exists so Renz-Fi remains operational when SD is unavailable.  
> It is **not** a replacement for SD and must not store large application datasets.

| Tier | Medium | Role |
|------|--------|------|
| 1 | NVS + SPIFFS `/fb/` | Bounded operational checkpoints |
| 2 | SD card | Canonical mutable runtime / bulk data |
| 3 | RAM / PSRAM | Volatile working memory only |

## Already implemented (shared firmware — Waveshare uses the same `StorageManager`)

- `writeJson` / `readJson` fall through to SPIFFS `/fb/` for fallback-eligible paths when SD is not writable
- NVS: owner/operator credentials (`renz-auth`), network settings (`renz-network`)
- Bounded emergency spools for financial/session history during SD outage
- `syncFallbackToSd()` on remount; conflicts are recorded, **no auto-merge**
- Setup status treats `healthy || usingFallback` as operational storage

## Setup-critical vs bulk (classification)

| Data | SD primary | Internal fallback | Notes |
|------|------------|-------------------|-------|
| Owner/operator credentials | — | **NVS** | Not duplicated as plaintext SD secrets |
| **Setup Unlock Key** | SD mirror in provisioning.json | **NVS `renz-auth` (`unlockHash` / `unlockBlob`)** | Appliance-bound; survives SD replace/erase; factory reset clears |
| Network address mode | NVS + optional SD sync | **NVS** | Boot before SD |
| installation.json | Yes | `/fb/installation.json` | Setup continuity |
| provisioning.json | Yes | `/fb/provisioning.json` | Setup continuity |
| router-connection.json | Yes | `/fb/router-connection.json` | Protected credentials |
| router.json / settings / promos | Yes | `/fb/*` bounded | Operational checkpoint |
| portal sessions / vouchers (active) | Yes | Bounded `/fb` | Emergency only |
| sales.json (bounded) | Yes | Bounded `/fb` | Not full history |
| History / charts / media / backups / exports | **SD only** | No unlimited mirror | Must not grow SPIFFS |

## Waveshare-specific continuity fixes (this checkpoint)

1. **Admin RouterOS gate:** Block only during `Mounting` / `Remounting` / `Syncing`. Steady `SD_DEGRADED` with SPIFFS fallback no longer permanently returns `ROUTER_RECOVERY_IN_PROGRESS` (would treat SD absence as appliance downtime).
2. **Boot without SD:** Seed bounded SPIFFS checkpoints for installation, provisioning, settings, promos, router template, portal config, portal sessions, and sales — matching the N16R8 “setup can start without SD” product rule.

## Explicitly not claimed

- This does **not** fix W5500 `SPI_DMA_ALLOCATION_FAILED` under remount+RouterOS overlap.
- This does **not** make SD optional for production bulk data.
- PSRAM remains volatile working memory (Phase 1A portal-save); not persistent storage.

## Physical validation still required

See TEST A–I in the storage-resilience implementation prompt: SD present/absent, remove during setup/production/portal, remount reconcile, reboot without SD, DMA and MikroTik CPU monitoring.
