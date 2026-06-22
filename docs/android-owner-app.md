# Android owner app — integration notes

The Renz-Fi **owner interface** is the React admin dashboard served by the ESP32 appliance. PWA installation has been removed from the web UI; the official owner experience will be a **dedicated Android application** that discovers the appliance and loads the dashboard in a WebView.

## Official entry points

| Purpose | URL |
|--------|-----|
| Admin dashboard (WebView load target) | `http://<appliance-ip>/admin` |
| Appliance discovery / health check | `http://<appliance-ip>/api/health` |

Default appliance IP on VLAN40: **`10.40.0.2`**

Example:

- Dashboard: `http://10.40.0.2/admin`
- Health: `http://10.40.0.2/api/health`

The `/admin` route serves the same React SPA as `/login` and `/dashboard`; it redirects authenticated users to the dashboard and unauthenticated users to login.

---

## Appliance discovery API

### `GET /api/health`

Unauthenticated. Used by the Android app to confirm the device is a reachable Renz-Fi appliance before opening the WebView.

**Request**

```http
GET /api/health HTTP/1.1
Host: 10.40.0.2
Accept: application/json
```

No request body. Cookies are optional (session fields in the response reflect login state if present).

**Success response** — HTTP `200`

Minimum check for discovery: parse JSON and verify `"success": true`.

```json
{
  "success": true,
  "data": {
    "ok": true,
    "storage": {
      "ok": true
    },
    "session": {
      "authenticated": false,
      "mustChangePassword": false
    }
  },
  "message": "OK"
}
```

**Discovery rule (recommended)**

```kotlin
// Pseudocode
val response = http.get("http://10.40.0.2/api/health")
val json = parseJson(response.body)
return response.status == 200 && json.success == true
```

**Failure**

- Network unreachable, timeout, or connection refused → treat as appliance offline.
- HTTP `4xx` / `5xx` or `success: false` → show connection help; do not load the WebView.

**Notes**

- Response includes `data.session.authenticated` for optional UI (e.g. skip login hint). Discovery only requires `success: true`.
- CORS is configured on the appliance; native Android HTTP clients are not subject to browser CORS.
- Use a short timeout (e.g. 3–5 s) on LAN discovery.

---

## Recommended Android app flow

```
Splash Screen
      ↓
Checking appliance...
      ↓
GET http://10.40.0.2/api/health
      ↓
   Reachable?
    ↙        ↘
  YES         NO
   ↓           ↓
Open WebView   Show Connection Help
http://10.40.0.2/admin
```

### Splash / checking

- Show branding and “Checking appliance…” while `GET /api/health` runs.
- Allow configured or remembered appliance IP (default `10.40.0.2`).

### Reachable (`success: true`)

1. Open WebView to `http://<appliance-ip>/admin`.
2. Enable JavaScript, DOM storage, and cookies (session auth uses HTTP cookies).
3. Handle in-WebView login (`/login`) and password change flows as today in the browser.

### Not reachable

Show **Connection Help**, for example:

- Phone must be on the same LAN / Wi‑Fi as the appliance (VLAN40).
- Verify appliance IP (default `10.40.0.2`).
- Confirm ESP32 is powered and Ethernet link is up.
- Retry health check.

---

## WebView considerations

- **Cookies**: Admin auth uses session cookies; use a WebView cookie manager and persist cookies between app restarts if desired.
- **Mixed content**: Dashboard is HTTP on LAN; WebView should allow cleartext for the appliance IP (Android `usesCleartextTraffic` or network security config scoped to private IPs).
- **Back navigation**: Map system back to WebView history inside the dashboard; exit app from root admin screen if appropriate.
- **No PWA install UI**: The web app no longer exposes install prompts or PWA status; the native app *is* the installed owner experience.

---

## Static assets retained on appliance

These files remain on SPIFFS for caching and future use but are **not** wired to any install UI:

- `/manifest.webmanifest`
- `/sw.js`

The React admin bundle does not register a service worker or listen for `beforeinstallprompt`.

---

## Local development

| Service | URL |
|---------|-----|
| Vite admin UI | `http://localhost:5173` |
| Simulator API | `http://127.0.0.1:3001/api/health` |

The simulator returns the same `{ "success": true, "data": { ... } }` envelope as the ESP32 firmware.

---

## Related code

- ESP32 route: `ESP32_S3_Firmware/src/ApiServer.cpp` — `GET /api/health`
- Simulator route: `server/index.ts` — `GET /api/health`
- Admin SPA routes: `src/App.tsx` — `/admin`, `/login`, `/dashboard`
