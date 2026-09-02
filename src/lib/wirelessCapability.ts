import type { RouterWireless } from "@/services/router";
import type { SystemStatus } from "@/types/api";

export type WirelessCapabilityState = "external_ap_only" | "wireless_capable" | "unknown";

/** Bridge-only routers (hEX, RB750, etc.) — no MikroTik wireless hardware. */
export function isBridgeOnlyRouterBoard(boardName?: string, identity?: string): boolean {
  const label = `${boardName ?? ""} ${identity ?? ""}`.toLowerCase();
  return (
    label.includes("hex") ||
    label.includes("rb750") ||
    label.includes("rb760") ||
    label.includes("rb1100") ||
    label.includes("rb2011") ||
    label.includes("ccr")
  );
}

/** Bridge-only board names (hEX, etc.) plus provisioning flags drive capability. */
export function resolveWirelessCapability(input: {
  wireless?: RouterWireless | null;
  networkProvisioning?: SystemStatus["networkProvisioning"];
  routerBoardName?: string;
  routerIdentity?: string;
}): WirelessCapabilityState {
  if (
    isBridgeOnlyRouterBoard(input.routerBoardName, input.routerIdentity)
  ) {
    return "external_ap_only";
  }

  const externalApOnly =
    input.wireless?.externalApOnly === true ||
    input.networkProvisioning?.externalApOnly === true ||
    input.networkProvisioning?.guestTopologyMode === "external_access_point" ||
    input.networkProvisioning?.noWirelessCapabilityDetected === true;

  if (externalApOnly) {
    return "external_ap_only";
  }

  const hasWirelessPath =
    input.wireless?.configured === true ||
    Boolean(input.wireless?.interface?.trim()) ||
    Boolean(input.wireless?.interfaceId?.trim()) ||
    input.wireless?.wifiMode === "new" ||
    input.wireless?.wifiMode === "existing";

  if (hasWirelessPath) {
    return "wireless_capable";
  }

  return "unknown";
}
