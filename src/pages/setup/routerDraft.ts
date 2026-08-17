import type { DetectRoutersResponse, RouterDriverManifest } from "@/types/routerProvisioning";

const STORAGE_KEY = "renz_setup_router_draft";

export type RouterWizardDraft = {
  selectedDriverId: string | null;
  selectedManifest: RouterDriverManifest | null;
  detectCache: DetectRoutersResponse | null;
  firmware: string;
  version: string;
  host: string;
  username: string;
  profile: string;
  rememberCredentials: boolean;
};

const defaultDraft = (): RouterWizardDraft => ({
  selectedDriverId: null,
  selectedManifest: null,
  detectCache: null,
  firmware: "",
  version: "",
  host: "",
  username: "",
  profile: "",
  rememberCredentials: true,
});

export function readRouterDraft(): RouterWizardDraft {
  try {
    const raw = sessionStorage.getItem(STORAGE_KEY);
    if (!raw) return defaultDraft();
    return { ...defaultDraft(), ...(JSON.parse(raw) as Partial<RouterWizardDraft>) };
  } catch {
    return defaultDraft();
  }
}

export function writeRouterDraft(partial: Partial<RouterWizardDraft>): RouterWizardDraft {
  const next = { ...readRouterDraft(), ...partial };
  sessionStorage.setItem(STORAGE_KEY, JSON.stringify(next));
  return next;
}

export function clearRouterDraft(): void {
  sessionStorage.removeItem(STORAGE_KEY);
}
