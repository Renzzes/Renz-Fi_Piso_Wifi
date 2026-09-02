import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import tsconfigPaths from "vite-tsconfig-paths";
import path from "node:path";
import { writeFileSync } from "node:fs";

const appBuildId = new Date().toISOString();

/**
 * OpenWrt Router Edition Admin build.
 *
 * Overlay flash does not have the ESP32 SPIFFS 32-byte object-name limit, so
 * chunks keep readable names. Output is staged by scripts/stage-openwrt-www.mjs
 * into openwrt/files/www/renzfi/.
 *
 * Run: npm run build:openwrt
 */
const openwrtOutput = {
  assetFileNames: "assets/[name]-[hash][extname]",
  chunkFileNames: "assets/[name]-[hash].js",
  entryFileNames: "assets/[name]-[hash].js",
} as const;

export default defineConfig({
  define: {
    __APP_BUILD_ID__: JSON.stringify(appBuildId),
    __RENZFI_EDITION__: JSON.stringify("openwrt"),
  },
  plugins: [
    react(),
    tailwindcss(),
    tsconfigPaths(),
    {
      name: "emit-admin-build-json",
      closeBundle() {
        writeFileSync(
          path.resolve(__dirname, "dist-openwrt/admin-build.json"),
          JSON.stringify(
            { adminBuild: appBuildId, edition: "openwrt" },
            null,
            2,
          ),
        );
      },
    },
  ],
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
    outDir: "dist-openwrt",
    emptyOutDir: true,
    assetsDir: "assets",
    assetsInlineLimit: 0,
    sourcemap: false,
    rollupOptions: {
      output: {
        ...openwrtOutput,
        manualChunks(id) {
          if (id.includes("src/pages/setup/screens/")) {
            return "setup-screens";
          }
        },
      },
    },
  },
});
