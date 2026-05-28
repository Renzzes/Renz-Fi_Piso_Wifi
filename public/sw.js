const CACHE = "renz-fi-admin-v2";

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE).then(() => self.skipWaiting()));
});

self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener("fetch", (event) => {
  const { request } = event;
  if (request.method !== "GET") return;
  const url = new URL(request.url);
  if (url.pathname.startsWith("/api")) return;

  const isNavigation = request.headers.get("accept")?.includes("text/html");
  const isStatic =
    url.pathname === "/" ||
    url.pathname === "/index.html" ||
    url.pathname.startsWith("/assets/") ||
    /\.(js|css|png|jpg|jpeg|svg|webp|ico|webmanifest)$/.test(url.pathname);

  // Offline shell caching only: do not cache dynamic pages or realtime API responses.
  if (!isNavigation && !isStatic) return;

  event.respondWith(
    caches.match(request).then((cached) => {
      const network = fetch(request).then((response) => {
        if (
          response &&
          response.ok &&
          url.origin === self.location.origin &&
          (isStatic || isNavigation)
        ) {
          caches.open(CACHE).then((cache) => cache.put(request, response.clone()));
        }
        return response;
      });

      return cached ?? network;
    }),
  );
});
