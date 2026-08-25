import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import tsconfigPaths from "vite-tsconfig-paths";
import path from "node:path";
import { writeFileSync } from "node:fs";

const appBuildId = new Date().toISOString();

/**
 * ESP32 SPIFFS object names are limited to 32 bytes (full path from volume root).
 * Example failure: /assets/setup-screens-osXFnkDn.js (33 chars).
 *
 * Set EMBEDDED_BUILD=1 for production builds staged to ESP32 (npm run build / build:esp32).
 * Omit for readable chunk names (npm run build:web).
 */
const isEmbeddedBuild =
  process.env.EMBEDDED_BUILD === "1" || process.env.EMBEDDED_BUILD === "true";

/** SPIFFS-safe: /assets/[8-char hash].js → 19 chars max under /assets/ */
const embeddedOutput = {
  chunkFileNames: "assets/[hash:8].js",
  entryFileNames: "assets/[hash:8].js",
  assetFileNames: "assets/[hash:8][extname]",
} as const;

const standardOutput = {
  assetFileNames: "assets/[name]-[hash][extname]",
  chunkFileNames: "assets/[name]-[hash].js",
  entryFileNames: "assets/[name]-[hash].js",
} as const;

export default defineConfig({
  define: {
    __APP_BUILD_ID__: JSON.stringify(appBuildId),
  },
  optimizeDeps: {
    include: ["exceljs"],
  },
  plugins: [
    react(),
    tailwindcss(),
    tsconfigPaths(),
    isEmbeddedBuild && {
      name: "emit-admin-build-json",
      closeBundle() {
        writeFileSync(
          path.resolve(__dirname, "dist/admin-build.json"),
          JSON.stringify({ adminBuild: appBuildId }, null, 2),
        );
      },
    },
  ].filter(Boolean),
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
  server: {
    port: 5173,
    proxy: {
      "/api": {
        target: "http://127.0.0.1:3001",
        changeOrigin: true,
      },
    },
  },
  build: {
    outDir: "dist",
    assetsDir: "assets",
    assetsInlineLimit: 0,
    sourcemap: false,
    rollupOptions: {
      output: {
        ...(isEmbeddedBuild ? embeddedOutput : standardOutput),
        manualChunks(id) {
          if (id.includes("src/pages/setup/screens/")) {
            return "setup-screens";
          }
        },
      },
    },
  },
});
