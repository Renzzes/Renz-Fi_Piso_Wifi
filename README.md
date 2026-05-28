# Renz-Fi Admin

Renz-Fi is moving toward a self-contained embedded Piso WiFi appliance. The production target is an ESP32-S3 device that serves the React PWA, hosts a lightweight local REST API, owns local storage, talks to the coin hardware and MikroTik router, and keeps operating when the admin dashboard is closed.

The Node.js/Express/SQLite code in this repository is now a development simulator only. It must not be treated as the commercial production runtime.

## Final Product Direction

- **Production host:** ESP32-S3 appliance, no external PC, laptop, Raspberry Pi, or Node.js runtime.
- **Frontend:** existing React PWA served as static files by the device.
- **Backend:** embedded REST API in firmware, using compact JSON and simple same-origin `/api/*` endpoints.
- **Storage:** future LittleFS, SPIFFS, SD card, or compact JSON persistence. SQLite remains simulator-only.
- **Realtime:** lightweight SSE-style updates with low-frequency polling fallback.
- **Dashboard role:** view and control interface only. The ESP32 is the source of truth and keeps running independently.

## Development

```bash
npm install
npm run dev
```

- UI: http://localhost:5173 (proxies `/api` → backend)
- Simulator API: http://localhost:3001

For static PWA output:

```bash
npm run build
```

Production firmware work is intentionally not implemented here yet.

## Embedded API Boundary

The frontend should depend on these embedded-friendly API areas, not Express, SQLite, or Node-specific response details:

- `GET /api/status`
- `/api/system`
- `/api/promos`
- `/api/vouchers`
- `/api/users`
- `/api/sales`
- `/api/settings`
- `/api/logs`
- `/api/coin`
- `/api/router`

Payloads should stay small, shallow, and local-first. Heavy analytics, oversized logs, large database exports, queue internals, Node memory metrics, and middleware-specific fields belong only in the simulator or future service tools, not the embedded appliance contract.

## Migration Notes

- Keep the current UI, routes, cards, tables, and responsive behavior visually unchanged.
- Keep all API calls behind `src/services/*` and `src/services/embeddedApi.ts`.
- Treat `server/` as a simulator for local development while firmware endpoints are not available.
- Do not add cloud auth, OAuth, Firebase, heavy JWT flows, WebSockets, or enterprise reporting to the embedded path.
- Future firmware should serve the built `dist/` PWA and implement the same `/api/*` contract from the device LAN address, normally `http://10.10.10.1`.
