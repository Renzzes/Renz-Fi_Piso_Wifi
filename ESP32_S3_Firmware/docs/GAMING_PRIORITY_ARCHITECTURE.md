# Gaming Priority Architecture (Pilot)

Gaming Priority adds a guest-network QoS layer on MikroTik without changing HotSpot
user-profile `rate-limit` profiles (`renzfi-speed-*`).

## Flow

```
Admin Dashboard
  → GET/PUT /api/gaming-priority (persist /config/gaming-priority.json)
  → POST /api/gaming-priority/apply (manual)
  → RouterProvisioningWorker (OpType::GamingPrioritySync)
  → GamingPriorityRouterApply (inline in RouterProvisioningWorker)
  → RouterOsClient
```

Save does **not** auto-apply. Apply is owner-triggered and uses the single router
worker slot with DMA gating via `openPersistedRouterClient`.

## Ownership prefix

All RouterOS objects use the `renzfi-gp-` prefix and `renzfi-gp:` comments.

Pilot objects:

- Mangle connection marks per enabled game profile
- Mangle packet mark (`renzfi-gp:pkt-mark`)
- PCQ type `renzfi-gp-pcq-download`
- Queue tree `renzfi-gp-qt-download` (download pilot only)

## Idempotency

Apply discovers owned objects by comment/name prefix, updates in place, creates
missing objects, and removes obsolete **owned** objects only. A second apply must
not duplicate rules.

## Limitations (pilot)

- Two seeded game profiles (Mobile Legends, Call of Duty Mobile)
- `dst-port` classification only
- Download queue tree only (no upload pilot)
- Real gaming traffic behavior not validated in firmware CI
