/**
 * FROZEN — Router driver manifest utilities (Phase 6C.2+)
 *
 * All router compatibility logic lives here. Do not duplicate in screens.
 *
 * • `unsupportedReason()` mirrors firmware RouterDriverManifest::unsupportedReason
 * • `isFirmwareSupported()` — preferred gate before API calls
 * • `compareRouterVersions()` — dotted version compare (mirrors firmware)
 *
 * Future drivers: extend manifest types in routerProvisioning.ts only;
 * compatibility checks must use helpers from this file.
 */
import type {
  DriverDetectionEntry,
  RouterCapabilities,
  RouterDriverManifest,
} from "@/types/routerProvisioning";

/** Mirror RouterDriverManifest::unsupportedReason (firmware-side). */
export function unsupportedReason(
  manifest: RouterDriverManifest,
  firmware: string,
  version: string,
): string {
  const fw = firmware.trim();
  const ver = version.trim();
  const supported = manifest.supportedFirmware?.trim() ?? "";

  if (supported && fw && fw.toLowerCase() !== supported.toLowerCase()) {
    return `Firmware '${fw}' is not supported by the ${manifest.vendor} driver`;
  }
  if (manifest.minimumVersion && ver && compareRouterVersions(ver, manifest.minimumVersion) < 0) {
    return `Firmware version ${ver} is below the minimum supported ${manifest.minimumVersion}`;
  }
  return "";
}

/** True when firmware + version pass manifest compatibility rules. */
export function isFirmwareSupported(
  manifest: RouterDriverManifest,
  firmware: string,
  version: string,
): boolean {
  return unsupportedReason(manifest, firmware, version) === "";
}

function readVersionComponent(value: string, index: { i: number }): number {
  let component = 0;
  while (index.i < value.length && /\d/.test(value[index.i] ?? "")) {
    component = component * 10 + Number(value[index.i]);
    index.i += 1;
  }
  return component;
}

/** Compare dotted version strings — mirrors firmware compareRouterVersions. */
export function compareRouterVersions(left: string, right: string): number {
  const li = { i: 0 };
  const ri = { i: 0 };

  for (let part = 0; part < 8; part += 1) {
    const leftPart = readVersionComponent(left, li);
    const rightPart = readVersionComponent(right, ri);
    if (leftPart !== rightPart) return leftPart > rightPart ? 1 : -1;
    if (li.i < left.length && left[li.i] === ".") li.i += 1;
    if (ri.i < right.length && right[ri.i] === ".") ri.i += 1;
    if (li.i >= left.length && ri.i >= right.length) break;
  }
  return 0;
}

export function mergeDriverEntries(
  available: RouterDriverManifest[] = [],
  drivers: DriverDetectionEntry[] = [],
): Array<{ manifest: RouterDriverManifest; detection: DriverDetectionEntry }> {
  const byId = new Map<string, RouterDriverManifest>();
  for (const item of available) {
    if (item.driverId) byId.set(item.driverId, item);
  }

  if (drivers.length > 0) {
    return drivers.map((detection) => {
      const manifest =
        detection.manifest ??
        byId.get(detection.driverId) ?? {
          driverId: detection.driverId,
          vendor: detection.driverId,
        };
      return { manifest, detection };
    });
  }

  return available.map((manifest) => ({
    manifest,
    detection: { driverId: manifest.driverId },
  }));
}

export function formatConfidence(confidence?: string | number): string {
  if (confidence == null || confidence === "") return "Unknown";
  if (typeof confidence === "number") {
    return `${Math.round(confidence * 100)}%`;
  }
  return confidence.replace(/_/g, " ");
}

export function capabilityLabels(caps?: RouterCapabilities): string[] {
  if (!caps) return [];
  const labels: Array<[keyof RouterCapabilities, string]> = [
    ["supportsHotspot", "Hotspot"],
    ["supportsApi", "API"],
    ["supportsVoucherControl", "Vouchers"],
    ["supportsBandwidthLimit", "Bandwidth limits"],
    ["supportsIdentity", "Identity"],
    ["supportsHealth", "Health"],
    ["supportsStatistics", "Statistics"],
    ["supportsRemoteConfig", "Remote config"],
  ];
  return labels.filter(([key]) => caps[key]).map(([, label]) => label);
}

export function stabilityLabel(stability?: string): string {
  return stability === "experimental" ? "Experimental" : "Supported";
}
