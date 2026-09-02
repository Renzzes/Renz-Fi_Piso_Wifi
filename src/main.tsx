import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { BrowserRouter } from "react-router-dom";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { Toaster } from "@/components/ui/sonner";
import { initTheme, ThemeProvider } from "@/lib/theme";
import App from "./App.tsx";
import "./styles.css";

initTheme();

/** Clear leftover PWA controllers from older builds (cache-first shells looked like a stuck splash). */
if (typeof navigator !== "undefined" && "serviceWorker" in navigator) {
  void navigator.serviceWorker.getRegistrations().then((regs) => {
    for (const reg of regs) void reg.unregister();
  });
  if (typeof caches !== "undefined") {
    void caches.keys().then((keys) => {
      for (const key of keys) {
        if (key.startsWith("renz-fi-admin")) void caches.delete(key);
      }
    });
  }
}

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: (failureCount) => failureCount < 1 && navigator.onLine,
      staleTime: 5000,
      gcTime: 5 * 60 * 1000,
      refetchOnWindowFocus: false,
      refetchOnReconnect: false,
    },
  },
});

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <QueryClientProvider client={queryClient}>
      <BrowserRouter>
        <ThemeProvider>
          <App />
          <Toaster richColors position="top-right" />
        </ThemeProvider>
      </BrowserRouter>
    </QueryClientProvider>
  </StrictMode>,
);
