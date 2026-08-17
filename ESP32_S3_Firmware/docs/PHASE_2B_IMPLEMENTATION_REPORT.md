# Phase 2B — Installation State Sync + Router Connection Check

**Status:** Implemented  
**Scope:** Management AP setup plane only (`192.168.4.1`)

## Summary

Phase 2B fixes the Phase 2A state split between `SetupProvisioningManager` and
`InstallationStateManager`, then adds **Step 3 — Check Router Connection** to
the first-run wizard. No MikroTik configuration or RouterOS probing is performed.

## Part A — Single source of truth

**Authoritative installation lifecycle:** `/config/installation.json` via
`InstallationStateManager`.

**Owner account metadata only:** `/config/provisioning.json` via
`SetupProvisioningManager` (`ownerCreated`, username, display name, password hash).

Extended `InstallationState` enum:

```
factory → owner_created → router_configured → provisioned → … → ready
```

### Migration / repair (boot)

`SetupProvisioningManager::synchronizeAtBoot()` runs after both managers load:

1. **Phase 2A repair:** If `provisioning.json` has `ownerCreated=true` but
   `installation.json` is still `factory`, advance installation to `owner_created`
   (never downgrade).
2. **Schema v2:** Remove legacy `installationState` field from `provisioning.json`.
3. **Flag repair:** If installation is already `>= owner_created` and NVS shows
   `firstBootCompleted`, set `ownerCreated=true` in provisioning when missing.

### Owner creation sync

`POST /api/setup/owner` success path:

1. Persist owner fields to `provisioning.json`
2. `InstallationStateManager::advanceTo(OwnerCreated)`
3. Serial: `[setup] installation state synchronized: owner_created`

### Heartbeat / lifecycle

- `install=` in heartbeat uses `installationStateLabel()` from
  `InstallationStateManager` (e.g. `owner_created`).
- `lifecycle=FactoryProvisioning` only when installation state is `factory`.
- After owner creation: `lifecycle=SetupApReady` while Management AP is active.

## Part B — Router Connection Check

### Endpoint

`GET /api/setup/router-status` (Management AP only)

**Example response:**

```json
{
  "success": true,
  "message": "Router connection status",
  "data": {
    "installationState": "owner_created",
    "ethernetLink": true,
    "dhcpReady": true,
    "espIp": "10.10.10.2",
    "gatewayIp": "10.10.10.1",
    "dnsIp": "10.10.10.1",
    "routerDetected": true,
    "connectionState": "router_detected",
    "nextStep": "router_configuration"
  }
}
```

`routerDetected=true` when link is up, DHCP IP is valid (non-zero), and gateway
is valid (non-zero). No RouterOS API calls.

### Wizard

Three steps: Device Check → Create Owner Account → Check Router Connection.

Step 3 auto-refreshes every 2.5s while visible. Shows green
“MikroTik connection detected.” when `routerDetected=true`. Does **not** advance
to `router_configured`.

### Serial logs

- `[setup] installation state synchronized: <state>`
- `[setup] router check: link=<up/down> dhcp=<ready/waiting> ip=<IP|none> gateway=<IP|none>`

## Files changed

| File | Change |
|------|--------|
| `src/InstallationState.h/.cpp` | Added `owner_created`, `router_configured`, `provisioned` |
| `src/InstallationStateManager.h` | `Provisioned` counts as ready |
| `src/SetupProvisioningManager.h/.cpp` | Delegates lifecycle to InstallationStateManager; boot sync |
| `src/web/SetupServer.cpp` | Step 3 UI + `GET /api/setup/router-status` |
| `src/FirmwareApp.cpp` | Boot sync + lifecycle fix |
| `src/provisioning/ProvisioningEngine.cpp` | Workflow labels for new states |
| `src/BootDiagnostics.cpp` | Display labels |
| `docs/HTTP_ROUTE_CONTRACT.md` | Route table |
| `docs/NETWORK_PLANE_ARCHITECTURE.md` | Setup plane routes + state ownership |
