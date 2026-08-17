#pragma once

#include <ESPAsyncWebServer.h>

class AuthManager;
class EthernetManager;

namespace HttpPlaneGate {

enum class Plane : uint8_t { Unknown = 0, Setup, Production };

/**
 * Request access class for production Ethernet traffic.
 *
 * Plane (Setup vs Production) is still based on the ESP32 *local* interface.
 * AccessClass further separates clients that share Ethernet arrival:
 *   - Management: same subnet as the ESP32 ETH address (admin LAN)
 *   - CustomerPortal: other remotes that reach ETH (typically Hotspot guests
 *     forwarded by MikroTik walled-garden)
 *
 * CustomerPortal may load the Admin login SPA and authenticate. Privileged
 * Admin APIs require Management OR a valid AuthManager session — never treat
 * the guest subnet as trusted Management.
 */
enum class AccessClass : uint8_t {
  Unknown = 0,
  Setup,
  Management,
  CustomerPortal,
};

void bindEthernet(EthernetManager *eth);
void bindAuth(AuthManager *auth);

Plane classify(AsyncWebServerRequest *req);
AccessClass classifyAccess(AsyncWebServerRequest *req);

bool isSetupPlane(AsyncWebServerRequest *req);
bool isProductionPlane(AsyncWebServerRequest *req);
bool isCustomerPortalClient(AsyncWebServerRequest *req);
bool isManagementClient(AsyncWebServerRequest *req);
bool hasAdminSession(AsyncWebServerRequest *req);

// Returns true when the request may proceed on the setup plane (192.168.4.1).
bool ensureSetupPlane(AsyncWebServerRequest *req);

// Returns true when the request may proceed on the production plane (active ETH IP).
bool ensureProductionPlane(AsyncWebServerRequest *req);

// Production plane AND Management LAN remote only (not Hotspot guests).
// Keep for Management-only surfaces (ESP32 portal recovery, downloads, etc.).
bool ensureManagementClient(AsyncWebServerRequest *req);

// Production plane + (Management LAN OR CustomerPortal). Admin login SPA / assets.
bool ensureAdminSpaClient(AsyncWebServerRequest *req);

// Production plane + (Management LAN OR authenticated Admin/Operator session).
// Privileged Admin APIs — session is authoritative for Hotspot remotes.
bool ensureAdminAccess(AsyncWebServerRequest *req);

// Auth + minimal onboarding APIs are allowed on setup or production planes.
bool ensureAppliancePlane(AsyncWebServerRequest *req);

const char *planeLabel(Plane plane);
const char *planeLabel(AsyncWebServerRequest *req);
const char *accessClassLabel(AccessClass access);
const char *accessClassLabel(AsyncWebServerRequest *req);

}  // namespace HttpPlaneGate
