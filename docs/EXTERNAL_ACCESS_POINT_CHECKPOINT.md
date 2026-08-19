# External Access Point Architecture Checkpoint

## Checkpoint Purpose

This checkpoint freezes the current External Access Point architecture after formalizing the RouterOS worker ownership exception for External AP Detect.

This is a checkpoint-only update. No new features, refactors, AP Check changes, RouterOS behavior changes, DMA fixes, or production tag changes are included.

## Production Baseline

- Production tag: `v0.5.0-fully-operational`
- Production baseline commit: `55a33ac289896a20c8687a5da0b623699eef19a7`
- Status: unchanged (rollback baseline preserved)

## Feature Branch

- Working branch: `feature/external-access-point`
- Stage B checkpoint (historical): `8ccc09285069e4c4cc87d926779f2caa336401fe`
- Stage C checkpoint (historical): `327feeeec2341cc4397fc04278491493a3ea92fb`
- History policy: no rewrite

## Current Detect Implementation (Frozen)

External AP Detect remains:

- Owner-triggered
- One-time
- Asynchronous (`POST` returns `202`)
- Bounded
- Read-only

Endpoints:

- `POST /api/access-points/detect`
- `GET /api/access-points/detect/jobs/{jobId}`

Allowed RouterOS reads:

- `/ip/arp/print`
- optional `/interface/bridge/host/print`
- optional `/ip/dhcp-server/lease/print`

Detect remains prohibited from RouterOS/AP writes, reconfiguration, periodic scanning, and continuous ARP monitoring.

## RouterProvisioningWorker Exception (Formalized)

`docs/EXTERNAL_ACCESS_POINT_ARCHITECTURE.md` now explicitly contains:

- `RouterProvisioningWorker` is the sole asynchronous RouterOS job owner.
- External AP Detect is a narrow approved exception that reuses the existing worker for one-time, bounded, read-only detection only.
- This exception does not authorize a second RouterOS worker/client/queue/owner and does not authorize AP provisioning or RouterOS writes.

## AP Check Separation (Unchanged)

AP Check remains independent from Detect and unchanged:

- Owner: `ap_check_worker`
- Driver: `GenericApDriver`
- Probe path: ICMP, TCP 80, TCP 443 fallback
- No RouterOS, no `RouterProvisioningWorker`, no SD writes, no `STORAGE_LOCK`, no AP credential usage

Detect status and Check status remain distinct and must not be merged.

## Guardrails Reconfirmed

- No AP provisioning/login through Renz-Fi
- Credentials remain metadata-only; API may expose `hasCredentials` only
- Vendor remains informational only; no vendor-specific AP driver additions
- No VLAN fields/provisioning/logic
- No AP-side bandwidth controls
- No additional RouterOS abstraction for Detect

## Test Results

Executed and passed:

- `node scripts/test-access-point-check-classification.mjs`
- `node scripts/test-access-point-detect.mjs`
- `node scripts/test-access-point-ip-validation.mjs`
- `node scripts/test-access-point-architecture-guardrails.mjs`
- `node scripts/test-sales-chart-buckets.mjs`
- `node scripts/test-storage-health-semantics.mjs`
- `node scripts/test-sales-uptime-aggregation.mjs`
- `node scripts/test-portal-resolver.mjs`

## Build Results

- `npm run build:esp32`: passed
- `pio run -e freenove_esp32_s3_wroom`: unavailable in this environment (`pio` command not found)

## Validation Status

CODE VALIDATION COMPLETE.

PHYSICAL VALIDATION NOT YET PERFORMED.
