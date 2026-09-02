import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  PLACEHOLDER,
  buildPortalUrls,
  resolveApplianceBaseUrl,
  validateGeneratedAppJs,
  validateRouterOsTokens,
  validateStatusHtml,
} from "./portal-resolver.mjs";

assert.equal(
  resolveApplianceBaseUrl("http://10.10.10.2/", null),
  "http://10.10.10.2",
);
assert.equal(
  resolveApplianceBaseUrl(PLACEHOLDER, "http://192.168.4.1"),
  "http://192.168.4.1",
);
assert.equal(resolveApplianceBaseUrl("", null), "");
assert.equal(resolveApplianceBaseUrl("not-a-url", null), "");

const urls = buildPortalUrls("http://10.10.10.2");
assert.deepEqual(urls, {
  applianceBase: "http://10.10.10.2",
  apiBase: "http://10.10.10.2/api/portal",
  brandingBase: "http://10.10.10.2",
  eventsBase: "http://10.10.10.2",
});

const badJs = `var RENZFI_APPLIANCE_BASE_URL = "${PLACEHOLDER}";`;
assert.deepEqual(
  validateGeneratedAppJs(badJs, ["10.40.0.2", "192.168.88.2"]),
  [
    "RENZFI_APPLIANCE_BASE_URL declaration still contains placeholder",
    "Unresolved __RENZFI_APPLIANCE_BASE_URL__ in production output",
    "RENZFI_APPLIANCE_BASE_URL still unsubstituted in production build",
    "RENZFI_APPLIANCE_BASE_URL declaration is missing a concrete http(s) URL",
  ],
);

const goodJs = 'var RENZFI_APPLIANCE_BASE_URL = "http://10.10.10.2";';
assert.deepEqual(
  validateGeneratedAppJs(goodJs, ["10.40.0.2", "192.168.88.2"]),
  [],
);

const loginHtml = '<strong id="ipAddress">$(ip)</strong> $(mac) $(link-login-only) $(link-orig) $(if chap-id) $(chap-id) $(chap-challenge) $(endif)';
assert.deepEqual(validateRouterOsTokens(loginHtml), []);

const goodStatus =
  '<link rel="stylesheet" href="renzfi-style.css" />' +
  '<strong id="ipAddress">$(ip)</strong>' +
  '<strong id="macAddress">$(mac)</strong>' +
  '<script src="renzfi-app.js"></script>';
assert.deepEqual(validateStatusHtml(goodStatus), []);
assert.ok(validateStatusHtml("Hi, user Bytes up Status refresh").length > 0);

const statusSrc = readFileSync(
  join(dirname(fileURLToPath(import.meta.url)), "..", "portal", "status.html"),
  "utf8",
);
assert.deepEqual(validateStatusHtml(statusSrc), []);
assert.ok(/src=["']\/?renzfi-app\.js/.test(statusSrc));
assert.ok(statusSrc.includes("renzfi-style.css"));
assert.ok(statusSrc.includes("$(ip)"));
assert.ok(statusSrc.includes("$(mac)"));
assert.ok(!/location\.(replace|assign|href)\s*=/.test(statusSrc));
assert.ok(!/http-equiv\s*=\s*["']refresh["']/i.test(statusSrc));

console.log("[test:portal-resolver] OK");
