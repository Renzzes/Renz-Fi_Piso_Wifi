declare const __APP_BUILD_ID__: string;

/** ISO timestamp injected at Vite build time; "development" in dev server. */
export const adminBuildId =
  typeof __APP_BUILD_ID__ !== "undefined" ? __APP_BUILD_ID__ : "development";
