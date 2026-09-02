const CACHE = "renz-fi-admin-v6";

/** Admin React SPA shell paths only — captive portal (/ and /portal/*) is excluded. */
const ADMIN_SHELL_PATHS = new Set([
  "/admin",
  "/login",
  "/dashboard",
  "/index.html",
]);

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE).then(() => self.skipWaiting()));
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches
      .keys()
      .then((keys) => Promise.all(keys.filter((key) => key !== CACHE).map((key) => caches.delete(key))))
      .then(() => self.clients.claim()),
  );
});

function isAdminShellPath(pathname) {
  if (ADMIN_SHELL_PATHS.has(pathname)) return true;
  if (pathname.startsWith("/dashboard")) return true;
  if (pathname.startsWith("/promo-rates")) return true;
  if (pathname.startsWith("/vouchers")) return true;
  if (pathname.startsWith("/active-users")) return true;
  if (pathname.startsWith("/sales-reports")) return true;
  if (pathname.startsWith("/captive-portal")) return true;
  if (pathname.startsWith("/coin-settings")) return true;
  if (pathname.startsWith("/system-configuration")) return true;
  if (pathname.startsWith("/router-settings")) return true;
  if (pathname.startsWith("/logs")) return true;
  if (pathname.startsWith("/firmware")) return true;
  if (pathname.startsWith("/system-settings")) return true;
  if (pathname.startsWith("/admin/")) return true;
  return false;
}

function isCaptivePortalPath(pathname) {
  return pathname === "/" || pathname.startsWith("/portal");
}

self.addEventListener("fetch", (event) => {
  const { request } = event;
  if (request.method !== "GET") return;

  const url = new URL(request.url);
  if (url.origin !== self.location.origin) return;
  if (url.pathname.startsWith("/api")) return;
  if (isCaptivePortalPath(url.pathname)) return;

  const isNavigation = request.mode === "navigate" || request.headers.get("accept")?.includes("text/html");
  const isStatic =
    isAdminShellPath(url.pathname) ||
    url.pathname.startsWith("/assets/") ||
    url.pathname.startsWith("/icons/") ||
    url.pathname === "/manifest.webmanifest" ||
    url.pathname === "/sw.js" ||
    url.pathname === "/favicon.svg" ||
    url.pathname === "/favicon.ico" ||
    /\.(js|css|png|jpg|jpeg|svg|webp|ico|webmanifest)$/.test(url.pathname);

  if (!isNavigation && !isStatic) return;

  event.respondWith(
    (async () => {
      // Navigations must be network-first so a reload cannot stick on a stale shell/splash.
      if (isNavigation) {
        try {
          const response = await fetch(request);
          if (response && response.ok) {
            const cache = await caches.open(CACHE);
            void cache.put(request, response.clone());
          }
          return response;
        } catch {
          const cached = await caches.match(request);
          if (cached) return cached;
          throw new Error("offline");
        }
      }

      const cached = await caches.match(request);
      const network = fetch(request).then((response) => {
        if (response && response.ok && isStatic) {
          void caches.open(CACHE).then((cache) => cache.put(request, response.clone()));
        }
        return response;
      });
      return cached ?? network;
    })(),
  );
});
