/**
 * Shared customer MikroTik Hotspot overlay file list.
 * SOURCE: edit portal/ only. GENERATED: deployment/mikrotik-hotspot + Final_Build_Portal.
 * Keep aligned with scripts/build-mikrotik-portal.mjs FINAL_UPLOAD_FILES.
 */

export const APPLIANCE_BASE_URL_DEFAULT = "http://10.10.10.2";
export const PLACEHOLDER = "__RENZFI_APPLIANCE_BASE_URL__";

/** Files owners upload into the router's existing hotspot/ directory. */
export const MIKROTIK_UPLOAD_FILES = [
  "login.html",
  "status.html",
  "renzfi-app.js",
  "renzfi-style.css",
  "md5.js",
  "Default-Banner.png",
  "bg_music.mp3",
  "coin.mp3",
  "success.mp3",
];

/** Must exist after a successful build:mikrotik-portal. */
export const REQUIRED_GENERATED_FILES = [
  "login.html",
  "status.html",
  "renzfi-app.js",
  "renzfi-style.css",
  "md5.js",
];
