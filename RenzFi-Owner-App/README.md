# Renz-Fi Manager (Android)

Official native manager app for Renz-Fi Piso WiFi vendo installations. Discover ESP32 appliances on the local network, verify health via `/api/health`, and load the existing React admin dashboard in a WebView at `/admin`.

Supports **multiple devices** with offline-safe local storage. No cloud, VPS, Firebase, or relay dependency.

## Project structure

```
RenzFi-Owner-App/
├── app/
│   ├── build.gradle.kts
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── java/com/renzfi/owner/
│       │   ├── MainActivity.kt
│       │   ├── RenzFiOwnerApp.kt
│       │   ├── data/
│       │   │   ├── datastore/DevicePreferences.kt
│       │   │   ├── network/RenzFiApiClient.kt, RenzFiApiService.kt
│       │   │   └── repository/DeviceRepository.kt
│       │   ├── model/
│       │   │   ├── ConnectionState.kt
│       │   │   ├── HealthResponse.kt
│       │   │   └── VendoDevice.kt
│       │   ├── ui/
│       │   │   ├── components/ (DeviceCard, DeleteConfirmDialog, …)
│       │   │   ├── navigation/NavGraph.kt
│       │   │   ├── screens/
│       │   │   └── theme/
│       │   ├── util/Constants.kt, NetworkUtils.kt, DateUtils.kt
│       │   ├── viewmodel/
│       │   └── webview/RenzFiWebView.kt
│       └── res/
├── build.gradle.kts
└── settings.gradle.kts
```

## Architecture

| Layer | Responsibility |
|-------|----------------|
| **UI (Compose)** | Device list, settings, dashboard WebView, device form/overview |
| **ViewModel (MVVM)** | `MainViewModel`, `DeviceListViewModel`, `DeviceFormViewModel`, … |
| **Repository** | `DeviceRepository` — CRUD, health checks, migration |
| **DataStore** | JSON-serialized `VendoDevice` list; migrates legacy single-host setting |
| **Retrofit** | `GET /api/health` with 5 s timeout |

### App flow

```
Splash → Checking appliance…
         ↓
    Device count?
    ├─ 1 device → health check → Dashboard or Connection Help
    └─ 2+ devices → Device Selection screen
```

- **Single device**: Same as before — auto health check, open WebView at `http://<esp32-ip>/admin`
- **Multiple devices**: JuanFi-style card list with online/offline status (refreshed every 30 s)
- **Future remote access**: MikroTik DDNS/public IP stored per device for planned WireGuard VPN (not active yet)

### Device model (`VendoDevice`)

| Field | Purpose |
|-------|---------|
| `name` | Display name (e.g. Main Branch) |
| `mikrotikDdns` | MikroTik DDNS hostname (future VPN) |
| `mikrotikPublicIp` | Optional public IP |
| `esp32LocalIp` | LAN IP (default `10.40.0.2`) |
| `lastSeen` / `isOnline` | From `/api/health` polling |

### Migration

Existing users with a saved **Appliance Host** are automatically migrated to a single device named **My Vendo** with that IP. No reconfiguration required.

## Build

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
.\gradlew.bat assembleDebug
```

Output: `app/build/outputs/apk/debug/app-debug.apk`

## Requirements

- Android 8.0+ (API 26)
- Phone on same LAN as ESP32 for local dashboard access

## Related docs

See [docs/android-owner-app.md](../docs/android-owner-app.md) for API details and WebView notes.
