#include "WebServerManager.h"



#include "AdminServer.h"

#include "ApiServer.h"

#include "AssetServer.h"

#include "CaptivePortalDetectionServer.h"

#include "Config.h"

#include "DownloadServer.h"

#include "EventBusRouteProvider.h"

#include "HttpPlaneGate.h"

#include "ManagementApConfig.h"

#include "PortalServer.h"

#include "RenzFiDebug.h"

#include "SetupServer.h"

#include "StaticFileServer.h"

#include "WebRequestDiagnostics.h"

#include "WebResponse.h"



struct WebServerManager::Subsystems {

  SetupServer                  setupServer;

  StaticFileServer             staticFiles;

  AssetServer                  assetServer;

  PortalServer                 portalServer;

  AdminServer                  adminServer;

  DownloadServer               downloadServer;

  EventBusRouteProvider        events;

  CaptivePortalDetectionServer captivePortalDetection;

};



WebServerManager::WebServerManager() = default;



WebServerManager::~WebServerManager() {

  if (_server && _setupStarted) {

    _server->end();

  }

  delete _subsystems;

  _subsystems = nullptr;

  delete _server;

  _server = nullptr;

}



void WebServerManager::ensureSubsystems() {

  if (!_subsystems) {

    _subsystems = new Subsystems();

  }

}



void WebServerManager::initialize(uint16_t port) {

  _port = port;

  if (!_server) {

    _server = new AsyncWebServer(_port);

  }

  ensureSubsystems();

  // Single owner for Access-Control-Allow-Origin on every AsyncWebServerResponse
  // (ctor copies DefaultHeaders). Must run at most once: initialize() is called
  // from FirmwareApp and again from startSetupPlane; DefaultHeaders::addHeader
  // always appends, so a second call produced ACAO "*, *" and Chrome rejected
  // CORS (heartbeat/terminate/events). Do not also add ACAO in addCorsHeaders.
  static bool accessControlAllowOriginRegistered = false;
  if (!accessControlAllowOriginRegistered) {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    accessControlAllowOriginRegistered = true;
  }

}



void WebServerManager::configure(const WebServerDependencies &deps) {

  _deps = deps;

  if (!_server) initialize(_port);

  _setupRegistry.clear();

  _productionRegistry.clear();

}



bool WebServerManager::registerSetupProvider(IWebRouteProvider *provider) {

  return _setupRegistry.registerProvider(provider);

}



bool WebServerManager::registerProductionProvider(IWebRouteProvider *provider) {

  return _productionRegistry.registerProvider(provider);

}



void WebServerManager::wireSetupProviders(const WebServerDependencies &deps) {

  ensureSubsystems();

  Subsystems &s = *_subsystems;



  // Permanent setup plane: lightweight routes only on 192.168.4.1.

  // Provisioning, auth, and full system APIs are Ethernet-only (production plane).

  s.setupServer.begin(deps.eth, deps.installation, deps.setupProvisioning,
                      deps.storage, deps.auth, deps.setupRouterConnection,
                      deps.routerProvisioning, deps.routerWorker,
                      deps.setupWizardConfig, deps.networkSettings,
                      deps.finishEngine);

  registerSetupProvider(&s.setupServer);

  s.captivePortalDetection.begin(deps.routerWorker);
  registerSetupProvider(&s.captivePortalDetection);

  if (deps.api) {
    deps.api->registerSetupRoutes(*this, deps.setupProvisioning,
                                  deps.routerProvisioning);
    registerSetupProvider(deps.api);
  }

}



void WebServerManager::wireProductionProviders(

    const WebServerDependencies &deps) {

  ensureSubsystems();

  Subsystems &s = *_subsystems;



  s.assetServer.begin(deps.assets, deps.storage, deps.portalConfig);

  s.portalServer.begin(deps.assets);

  s.adminServer.begin(&s.staticFiles);

  s.events.bind(deps.events);



  registerProductionProvider(&s.staticFiles);

  registerProductionProvider(&s.assetServer);

  registerProductionProvider(&s.events);

  if (deps.api) registerProductionProvider(deps.api);

  registerProductionProvider(&s.portalServer);

  registerProductionProvider(&s.adminServer);

  registerProductionProvider(&s.downloadServer);

}



void WebServerManager::registerAdminEntryRoute() {

  if (!_server || !_subsystems) return;



  // SoftAP (setup plane): entry URLs land on the wizard. Ethernet (production):

  // same paths serve the Admin SPA. First-match registration — do not also

  // register these in SetupServer with ensureSetupPlane (would steal ETH).

  auto servePlaneAwareEntry = [this](AsyncWebServerRequest *req,

                                     const char *timerLabel) {

    WebRequestDiagnostics::RequestTimer timer(req, timerLabel);

    if (HttpPlaneGate::isSetupPlane(req)) {

      // Serve the wizard on `/` (no 302). Laptop Chrome was stacking the

      // redirect hop with /admin/setup and drawing Step 1 twice.

      _subsystems->setupServer.servePage(req);

      return;

    }

    if (!HttpPlaneGate::ensureProductionPlane(req)) return;

    if (!_productionRegistered) {

      WebResponse::serveErrorJson(

          req, 503, "Production admin pending Ethernet IP",

          "PRODUCTION_PLANE_PENDING");

      return;

    }

    _subsystems->staticFiles.serveStaticOrIndex(req);

  };



  _server->on("/", HTTP_GET, [servePlaneAwareEntry](AsyncWebServerRequest *req) {

    servePlaneAwareEntry(req, "WebServer/root-entry");

  });

  _server->on("/login", HTTP_GET, [servePlaneAwareEntry](AsyncWebServerRequest *req) {

    servePlaneAwareEntry(req, "WebServer/login-entry");

  });

  _server->on("/dashboard", HTTP_GET,

              [servePlaneAwareEntry](AsyncWebServerRequest *req) {

                servePlaneAwareEntry(req, "WebServer/dashboard-entry");

              });

  _server->on("/admin", HTTP_GET, [servePlaneAwareEntry](AsyncWebServerRequest *req) {

    servePlaneAwareEntry(req, "WebServer/admin-entry");

  });



  Serial.println(

      "[web] Plane-aware GET /, /login, /dashboard, /admin (AP -> wizard HTML, ETH -> SPA)");

}



void WebServerManager::registerNotFoundHandler() {

  _server->onNotFound([this](AsyncWebServerRequest *req) {

    WebRequestDiagnostics::RequestTimer timer(req, "WebServer/notFound");



    if (HttpPlaneGate::isSetupPlane(req)) {

      WebResponse::serveErrorJson(

          req, 403,

          "This route is available only through the Ethernet dashboard",

          "SETUP_PLANE_RESTRICTED");

      return;

    }



    if (req->method() == HTTP_OPTIONS) {

      WebResponse::serveOptions(req);

      return;

    }



    if (_productionRegistry.dispatchNotFound(req)) return;

    if (_setupRegistry.dispatchNotFound(req)) return;

    req->send(404, "text/plain", "Not Found");

  });

}



void WebServerManager::startSetupPlane(const WebServerDependencies &deps) {

  if (_setupStarted) {

    Serial.println("[web] Setup plane already started — skipping");

    return;

  }



  initialize(_port);

  configure(deps);

  wireSetupProviders(deps);



  if (!_setupRoutesRegistered) {

    _setupRegistry.registerAll(*this);

    registerAdminEntryRoute();

    registerNotFoundHandler();

    _setupRoutesRegistered = true;

    Serial.printf("[web] Setup plane registered %u route providers\n",

                  (unsigned)_setupRegistry.count());

  }



  _server->begin();

  _setupStarted = true;

  Serial.println("[web] AsyncWebServer started once — setup plane active");

}



void WebServerManager::registerProductionPlane(

    const WebServerDependencies &deps) {

  if (_productionRegistered) {

    Serial.println("[web] Production plane already registered — skipping");

    return;

  }



  if (!_setupStarted) {

    Serial.println("[web] Production plane deferred — setup plane not started");

    return;

  }



  _deps = deps;

  _productionRegistry.clear();

  wireProductionProviders(deps);

  _productionRegistry.registerAll(*this);



  _productionRegistered = true;

  Serial.printf("[web] Production plane registered %u route providers "

                "(server not restarted)\n",

                (unsigned)_productionRegistry.count());

}



void WebServerManager::pollSetupWorkflows() {
  (void)_subsystems;
  (void)_setupStarted;
  (void)_deps;
}

void WebServerManager::fillHealth(JsonObject obj) const {

  obj["setupStarted"] = _setupStarted;

  obj["productionRegistered"] = _productionRegistered;

  obj["port"] = _port;

  obj["setupProviders"] = _setupRegistry.count();

  obj["productionProviders"] = _productionRegistry.count();

}



AsyncWebServer &WebServerManager::routeServer() {

  return *_server;

}


