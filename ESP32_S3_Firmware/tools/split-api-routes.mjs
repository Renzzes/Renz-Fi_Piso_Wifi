import fs from 'fs';

const path = 'src/ApiServer.cpp';
let content = fs.readFileSync(path, 'utf8');

if (!content.includes('#include "HttpPlaneGate.h"')) {
  content = content.replace(
    '#include "WebServerManager.h"',
    '#include "WebServerManager.h"\n#include "HttpPlaneGate.h"\n#include "WebRequestDiagnostics.h"'
  );
}

const macro = `
#define RENZFI_APPLIANCE_GATE(req) \\
  do { \\
    if (!HttpPlaneGate::ensureAppliancePlane((req))) return; \\
  } while (0)

#define RENZFI_PROD_GATE(req) \\
  do { \\
    if (!HttpPlaneGate::ensureProductionPlane((req), _eth)) return; \\
  } while (0)

`;

if (!content.includes('RENZFI_PROD_GATE')) {
  content = content.replace(
    '// ── Route registration ────────────────────────────────────────────────────────',
    macro + '// ── Route registration ────────────────────────────────────────────────────────'
  );
}

if (!content.includes('registerProductionRoutes')) {
  content = content.replace(
    'void ApiServer::registerRoutes(WebServerManager &web) {',
    'void ApiServer::registerProductionRoutes(WebServerManager &web) {'
  );
}

// Extract setup block markers
const prodStart = content.indexOf('  // ── Dashboard status ──────────────────────────────────────────────────────');
const prodEndNetwork = content.indexOf('  // ── Network status (Management AP + Ethernet) ─────────────────────────────');
const prodEndPostSetup = content.indexOf('  // ── Ethernet network config (DHCP-first, optional static) ────────────────');

if (prodStart < 0 || prodEndNetwork < 0 || prodEndPostSetup < 0) {
  console.error('markers not found', { prodStart, prodEndNetwork, prodEndPostSetup });
  process.exit(1);
}

const setupPrefix = content.indexOf('void ApiServer::registerProductionRoutes');
const setupStart = content.indexOf('  // ── CORS preflight', setupPrefix);
const setupPart1 = content.slice(setupStart, prodStart);
const networkBlock = content.slice(prodEndNetwork, prodEndPostSetup);
const setupRoutes = setupPart1 + '\n' + networkBlock;

let productionBody = content.slice(prodStart, prodEndNetwork) +
  content.slice(prodEndPostSetup);

// Add PROD_GATE to production lambdas
productionBody = productionBody.replace(
  /\[this\]\(AsyncWebServerRequest \*req\) \{\n/g,
  (m) => m + '    RENZFI_PROD_GATE(req);\n'
);

// Add APPLIANCE_GATE to setup lambdas
const setupGated = setupRoutes.replace(
  /\[this\]\(AsyncWebServerRequest \*req\) \{\n/g,
  (m) => m + '    RENZFI_APPLIANCE_GATE(req);\n'
).replace(
  /\[this\]\(AsyncWebServerRequest \*req\) \{\n    RENZFI_APPLIANCE_GATE\(req\);\n    String cookie/g,
  '[this](AsyncWebServerRequest *req) {\n    RENZFI_APPLIANCE_GATE(req);\n    String cookie'
);

const setupFn = `
void ApiServer::registerSetupRoutes(WebServerManager &web) {
  _server = &web.routeServer();
  if (!_server) return;

  Serial.println("[web] ApiServer registering setup-plane routes");

${setupGated}
}

`;

const registerRoutesFn = `
void ApiServer::registerRoutes(WebServerManager &web) {
  registerProductionRoutes(web);
}

`;

const prodFnStart = content.indexOf('void ApiServer::registerProductionRoutes');
const prodFnEnd = content.indexOf('const char *ApiServer::providerName');
const before = content.slice(0, prodFnStart);
const after = content.slice(prodFnEnd);

const prodFn = `void ApiServer::registerProductionRoutes(WebServerManager &web) {
  _server = &web.routeServer();
  if (!_server) return;

  Serial.println("[web] ApiServer registering production-plane routes");

${productionBody}
}

`;

const finalContent = before + setupFn + prodFn + registerRoutesFn + after;
fs.writeFileSync(path, finalContent);
console.log('ApiServer routes split OK');
