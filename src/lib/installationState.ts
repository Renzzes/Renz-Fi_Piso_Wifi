import { apiUrl, embeddedApi } from "@/services/embeddedApi";

/** Installation states that mean production Admin only — no React /setup APIs. */
export function isProductionInstallationState(
  state: string | undefined | null,
): boolean {
  const normalized = (state ?? "").trim().toLowerCase();
  return normalized === "ready" || normalized === "provisioned";
}

export async function fetchInstallationState(): Promise<string | null> {
  const res = await fetch(apiUrl(embeddedApi.health), { credentials: "include" });
  if (!res.ok) return null;
  const json = (await res.json()) as {
    data?: { installationState?: string; installation?: { state?: string } };
    installationState?: string;
    installation?: { state?: string };
  };
  const data = json.data ?? json;
  return data.installationState ?? data.installation?.state ?? null;
}
