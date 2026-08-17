/**
 * Single source of truth for ESP32 SPIFFS staging.
 * Keep in sync with PortalSpiffsLayout.h required portal assets.
 */

import { join } from "node:path";

export const REPO_ROOT_MARKER = "portal";

/** Canonical captive portal sources — edit only files in portal/ */
export const PORTAL_SOURCE_DIR = "portal";

/**
 * Required portal files for staging + uploadfs gate.
 * spiffsPath is the object path inside the SPIFFS volume.
 */
export const PORTAL_REQUIRED = [
  { source: "login.html", spiffsPath: "/portal/login.html", label: "login.html" },
  { source: "renzfi-app.js", spiffsPath: "/portal/renzfi-app.js", label: "renzfi-app.js" },
  { source: "renzfi-style.css", spiffsPath: "/portal/renzfi-style.css", label: "renzfi-style.css" },
  { source: "md5.js", spiffsPath: "/portal/md5.js", label: "md5.js" },
  { source: "favicon.ico", spiffsPath: "/portal/favicon.ico", label: "favicon.ico" },
];

/** Bundled portal assets — optional on SPIFFS (MikroTik Hotspot holds production media).
 *  Do not stage large audio into ESP32 data/ — recovers ~1 MB SPIFFS.
 *  Default-Banner.png stays small and remains useful for AssetResolver fallback.
 */
export const PORTAL_RECOMMENDED = [
  { source: "Default-Banner.png", spiffsPath: "/portal/Default-Banner.png", label: "Default-Banner.png" },
];

/** Media kept in portal/ for MikroTik builds only — NOT staged to ESP32 SPIFFS. */
export const PORTAL_MIKROTIK_ONLY_MEDIA = [
  { source: "bg_music.mp3", label: "bg_music.mp3" },
  { source: "coin.mp3", label: "coin.mp3" },
  { source: "success.mp3", label: "success.mp3" },
];

export const ADMIN_ROOT_FILES = [
  "index.html",
  "favicon.svg",
  "sw.js",
  "manifest.webmanifest",
];

export const SPIFFS_MAX_OBJECT_NAME = 32;

export const ESP32_DATA_DIR = join("ESP32_S3_Firmware", "data");
