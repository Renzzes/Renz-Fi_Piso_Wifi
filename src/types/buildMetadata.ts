/** Staged appliance build metadata from SPIFFS — GET /api/health `data.build`. */
export interface ApplianceBuildMetadata {
  firmwareVersion: string;
  adminBuild?: string;
  portalRevision?: string;
  gitCommit?: string;
  buildNumber?: number;
  stagedAt?: string;
  deviceProfileVersion?: number;
  storageContractVersion?: number;
  httpContractVersion?: number;
}

export function parseApplianceBuildMetadata(
  raw: unknown,
): ApplianceBuildMetadata | null {
  if (!raw || typeof raw !== "object") return null;
  const obj = raw as Record<string, unknown>;
  const firmwareVersion =
    typeof obj.firmwareVersion === "string" ? obj.firmwareVersion.trim() : "";
  if (!firmwareVersion) return null;

  return {
    firmwareVersion,
    adminBuild:
      typeof obj.adminBuild === "string" ? obj.adminBuild : undefined,
    portalRevision:
      typeof obj.portalRevision === "string" ? obj.portalRevision : undefined,
    gitCommit: typeof obj.gitCommit === "string" ? obj.gitCommit : undefined,
    buildNumber:
      typeof obj.buildNumber === "number" ? obj.buildNumber : undefined,
    stagedAt: typeof obj.stagedAt === "string" ? obj.stagedAt : undefined,
    deviceProfileVersion:
      typeof obj.deviceProfileVersion === "number"
        ? obj.deviceProfileVersion
        : undefined,
    storageContractVersion:
      typeof obj.storageContractVersion === "number"
        ? obj.storageContractVersion
        : undefined,
    httpContractVersion:
      typeof obj.httpContractVersion === "number"
        ? obj.httpContractVersion
        : undefined,
  };
}
