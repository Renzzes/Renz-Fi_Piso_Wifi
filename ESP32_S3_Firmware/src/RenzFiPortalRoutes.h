#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

// Registers supplementary customer-portal and admin static routes on the
// AsyncWebServer.  Primary route registration lives in ApiServer; duplicate
// registrations are harmless — AsyncWebServer matches the first handler.
inline void registerRenzFiPortalRoutes(AsyncWebServer &server, fs::FS &fs) {
  // Root → customer portal (backup handler; ApiServer registers "/" first)
  server.on("/", HTTP_GET, [&fs](AsyncWebServerRequest *req) {
    if (fs.exists("/portal/index.html")) {
      req->send(fs, "/portal/index.html", "text/html");
    } else {
      req->send(404, "text/plain", "Portal not found");
    }
  });

  // /portal/ subtree from SPIFFS (style.css, app.js, sounds/, …)
  server.serveStatic("/portal/", fs, "/portal/").setCacheControl("max-age=86400");

  // /assets/ subtree from SPIFFS (React bundles)
  server.serveStatic("/assets/", fs, "/assets/").setCacheControl("max-age=31536000");

  // Admin root (backup handler)
  server.on("/admin", HTTP_GET, [&fs](AsyncWebServerRequest *req) {
    if (fs.exists("/index.html")) {
      req->send(fs, "/index.html", "text/html");
    } else {
      req->send(404, "text/plain", "Admin not found");
    }
  });
}

// Serves /index.html for any /admin/* sub-path (React Router deep links).
// Returns true if the path matched and was handled; false otherwise.
inline bool serveRenzFiAdminSpaFallback(AsyncWebServerRequest *req, fs::FS &fs) {
  if (!req->url().startsWith("/admin/")) return false;

  if (fs.exists("/index.html")) {
    req->send(fs, "/index.html", "text/html");
  } else {
    req->send(404, "text/plain", "Admin not found");
  }
  return true;
}
