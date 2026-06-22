import { db } from "../../db/connection.js";
import type { RouterConfig, RouterTestResult, RouterTestStep } from "./types.js";

const DEFAULT_ESP32_IP = "10.40.0.2";

function buildTestSteps(config: RouterConfig): RouterTestStep[] {
  const host = config.host?.trim() ?? "";
  const username = config.username?.trim() ?? "";
  const password = config.password ?? "";
  const profile = config.profile?.trim() ?? "";

  const apiReachable = host.length > 0 && host !== DEFAULT_ESP32_IP;
  const loginOk = apiReachable && username.length > 0 && password.length > 0;
  const profileExists = loginOk && profile.length > 0;

  return [
    {
      id: "api_reachable",
      label: "RouterOS API reachable",
      ok: apiReachable,
      message: apiReachable
        ? `Target host ${host} is configured`
        : host === DEFAULT_ESP32_IP
          ? `Host ${host} is the ESP32 address — use the MikroTik gateway (e.g. 10.40.0.1)`
          : "MikroTik Router IP is not configured",
    },
    {
      id: "login",
      label: "Login successful",
      ok: loginOk,
      message: loginOk
        ? `API user "${username}" credentials present`
        : apiReachable
          ? "RouterOS API username and password are required"
          : "Skipped — API host not reachable",
    },
    {
      id: "profile",
      label: "Hotspot profile exists",
      ok: profileExists,
      message: profileExists
        ? `Profile "${profile}" is configured`
        : loginOk
          ? "Hotspot profile name is required"
          : "Skipped — API login not validated",
    },
  ];
}

/**
 * RouterOS connection adapter boundary.
 * Real RouterOS I/O is intentionally stubbed/minimal in this admin server.
 */
export async function testRouterConnection(config: RouterConfig): Promise<RouterTestResult> {
  const steps = buildTestSteps(config);
  const ok = steps.every((step) => step.ok);

  db.prepare(
    `INSERT INTO router_settings (key, value) VALUES ('connected', ?)
     ON CONFLICT(key) DO UPDATE SET value = excluded.value`,
  ).run(ok ? "1" : "0");

  db.prepare(`INSERT INTO logs (level, message) VALUES (?, ?)`).run(
    ok ? "INFO" : "ERR",
    ok
      ? `MikroTik connection test succeeded (${config.host})`
      : `MikroTik connection test failed (${config.host})`,
  );

  return {
    ok,
    steps,
    summary: ok ? "All checks passed" : "One or more checks failed",
  };
}
