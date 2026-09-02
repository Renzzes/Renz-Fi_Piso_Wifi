/**
 * Shared appliance base URL resolver — used by build validation and unit tests.
 * Keep in sync with resolveApplianceBaseUrl() in portal/renzfi-app.js.
 */
export const PLACEHOLDER = "__RENZFI_APPLIANCE_BASE_URL__";

export function resolveApplianceBaseUrl(configured, locationOrigin) {
  var value = configured == null ? "" : String(configured).trim();
  var isPlaceholder =
    value.length === 0 || value.indexOf(PLACEHOLDER) !== -1;
  if (isPlaceholder) {
    if (locationOrigin) return String(locationOrigin).replace(/\/+$/, "");
    return "";
  }
  value = value.replace(/\/+$/, "");
  if (!/^https?:\/\/.+/i.test(value)) return "";
  return value;
}

export function buildPortalUrls(applianceBase) {
  var base = resolveApplianceBaseUrl(applianceBase, null);
  if (!base) return null;
  return {
    applianceBase: base,
    apiBase: base + "/api/portal",
    brandingBase: base,
    eventsBase: base,
  };
}

export function validateGeneratedAppJs(source, forbiddenIps) {
  const errors = [];
  if (source.includes(`var RENZFI_APPLIANCE_BASE_URL = "${PLACEHOLDER}";`)) {
    errors.push("RENZFI_APPLIANCE_BASE_URL declaration still contains placeholder");
  }
  if (source.includes(PLACEHOLDER) &&
      /var\s+RENZFI_APPLIANCE_BASE_URL\s*=\s*["']__RENZFI_APPLIANCE_BASE_URL__["']/.test(source)) {
    errors.push("Unresolved __RENZFI_APPLIANCE_BASE_URL__ in production output");
  }
  // Fail if the assigned URL value is still the placeholder token.
  if (/var\s+RENZFI_APPLIANCE_BASE_URL\s*=\s*["'][^"']*__RENZFI_APPLIANCE_BASE_URL__[^"']*["']/.test(source)) {
    errors.push("RENZFI_APPLIANCE_BASE_URL still unsubstituted in production build");
  }
  if (/\bcredits\s*\*\s*5\b/.test(source) || /\bamount\s*\*\s*5\b/.test(source)) {
    errors.push("Legacy credits*5 / amount*5 calculation must not ship in MikroTik portal JS");
  }
  for (const ip of forbiddenIps) {
    if (source.includes(ip)) errors.push(`Forbidden stale IP remains: ${ip}`);
  }
  if (!/var RENZFI_APPLIANCE_BASE_URL = "https?:\/\/.+";/.test(source)) {
    errors.push("RENZFI_APPLIANCE_BASE_URL declaration is missing a concrete http(s) URL");
  }
  return errors;
}

export function validateRouterOsTokens(loginHtml) {
  const required = [
    "$(ip)",
    "$(mac)",
    "$(link-login-only)",
    "$(link-orig)",
    "$(if chap-id)",
    "$(chap-id)",
    "$(chap-challenge)",
    "$(endif)",
  ];
  return required.filter((token) => !loginHtml.includes(token));
}

/** Hotspot /status servlet shell — same Renz-Fi app, no native status UI, no redirects. */
export function validateStatusHtml(html) {
  const errors = [];
  if (!html.includes('id="ipAddress">$(ip)</strong>') && !html.includes("$(ip)")) {
    errors.push("status.html must expose MikroTik $(ip)");
  }
  if (!html.includes('id="macAddress">$(mac)</strong>') && !html.includes("$(mac)")) {
    errors.push("status.html must expose MikroTik $(mac)");
  }
  if (!/src=["']\/?renzfi-app\.js/.test(html)) {
    errors.push("status.html must load the same renzfi-app.js as login.html");
  }
  if (!html.includes("renzfi-style.css")) {
    errors.push("status.html must load the same renzfi-style.css as login.html");
  }
  if (/location\.(replace|assign|href)\s*=/.test(html)) {
    errors.push("status.html must not navigate with location.replace/assign/href");
  }
  if (/http-equiv\s*=\s*["']refresh["']/i.test(html)) {
    errors.push("status.html must not use meta refresh (redirect loop risk)");
  }
  if (/url\s*=\s*["']?\/login/i.test(html) || /href\s*=\s*["']\/login["']/i.test(html)) {
    errors.push("status.html must not redirect to /login");
  }
  if (/Hi,\s/.test(html) || /Bytes up/i.test(html) || /Status refresh/i.test(html)) {
    errors.push("status.html must not contain native MikroTik status UI");
  }
  return errors;
}
