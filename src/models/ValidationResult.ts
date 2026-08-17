import type { SetupStatus } from "@/components/setup/SetupStatusCard";
import type { SetupScreenId } from "@/components/setup/stepRouter";

/** Canonical validation read model — shared by wizard, diagnostics, health, support bundle. */
export interface ValidationResult {
  id: string;
  title: string;
  description: string;
  passed: boolean;
  severity: "info" | "warning" | "error";
}

export type ProvisioningCheckPayload = {
  id: string;
  passed: boolean;
  detail?: string;
  severity?: "info" | "warning" | "error";
};

/** Ordered validation categories displayed in Installation Verification. */
export const VALIDATION_CHECK_CATALOG: ReadonlyArray<{
  id: string;
  title: string;
}> = [
  { id: "router_connected", title: "Router" },
  { id: "portal_configured", title: "Portal" },
  { id: "coin_ready", title: "Coin" },
  { id: "storage_healthy", title: "Storage" },
  { id: "assets_ready", title: "Assets" },
  { id: "network_ready", title: "Network" },
  { id: "firmware_ready", title: "Firmware" },
];

const CHECK_GUIDANCE: Partial<Record<string, string>> = {
  router_connected: "Return to Router Connection and verify API credentials.",
  portal_configured: "Return to Portal Configuration and verify captive portal branding.",
  coin_ready: "Return to Coin Configuration and verify hardware diagnostics.",
  storage_healthy: "Ensure the SD card is inserted and formatted.",
  assets_ready: "Bundled portal assets are used by default; upload custom media from Admin later.",
  network_ready: "Verify router connectivity and hotspot profile selection.",
  firmware_ready: "Appliance firmware must match the supported driver manifest.",
};

/** Map backend checks into the canonical catalog (no client-side pass/fail logic). */
export function parseValidationResults(
  checks: ProvisioningCheckPayload[] | undefined,
): ValidationResult[] {
  const byId = new Map((checks ?? []).map((check) => [check.id, check]));

  return VALIDATION_CHECK_CATALOG.map(({ id, title }) => {
    const row = byId.get(id);
    if (!row) {
      return {
        id,
        title,
        description: "Pending validation",
        passed: false,
        severity: "info",
      };
    }

    const passed = row.passed === true;
    const severity =
      row.severity ?? (passed ? "info" : ("error" as ValidationResult["severity"]));

    return {
      id,
      title,
      description: row.detail?.trim() || (passed ? "Check passed" : "Check failed"),
      passed,
      severity,
    };
  });
}

export function validationResultToSetupStatus(
  result: ValidationResult,
  evaluated: boolean,
): SetupStatus {
  if (!evaluated || result.description === "Pending validation") {
    return "pending";
  }
  if (result.passed) {
    return "success";
  }
  if (result.severity === "warning") {
    return "warning";
  }
  return "error";
}

export function validationGuidanceFor(result: ValidationResult): string | undefined {
  if (result.passed) return undefined;
  return CHECK_GUIDANCE[result.id];
}

export function validationCheckTargetScreen(id: string): SetupScreenId | null {
  switch (id) {
    case "router_connected":
      return "router_connection";
    case "portal_configured":
      return "portal_configuration";
    case "coin_ready":
      return "coin_configuration";
    default:
      return null;
  }
}

export function partitionValidationResults(results: ValidationResult[]) {
  return {
    warnings: results.filter((r) => !r.passed && r.severity === "warning"),
    recommendations: results.filter((r) => r.passed && r.severity === "info"),
    failures: results.filter((r) => !r.passed && r.severity === "error"),
  };
}
