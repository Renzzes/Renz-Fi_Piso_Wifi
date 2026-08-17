import type { SetupScreenId } from "@/components/setup/stepRouter";
import { canNavigateToScreen } from "@/components/setup/navigationGuards";
import type { InstallationState } from "@/types/provisioning";

const SCREEN_KEY = "renz_setup_active_screen";
const SCROLL_KEY = "renz_setup_scroll_y";

export function persistSetupScreen(screen: SetupScreenId): void {
  try {
    sessionStorage.setItem(SCREEN_KEY, screen);
  } catch {
    /* quota / private mode */
  }
}

export function readPersistedSetupScreen(): SetupScreenId | null {
  try {
    const value = sessionStorage.getItem(SCREEN_KEY);
    return value as SetupScreenId | null;
  } catch {
    return null;
  }
}

export function clearPersistedSetupSession(): void {
  try {
    sessionStorage.removeItem(SCREEN_KEY);
    sessionStorage.removeItem(SCROLL_KEY);
  } catch {
    /* ignore */
  }
}

/** Restore UI screen after refresh when backend state still permits it. */
export function resolveRestoredSetupScreen(
  persisted: SetupScreenId | null,
  installationState: InstallationState,
): SetupScreenId | null {
  if (!persisted) return null;
  const guard = canNavigateToScreen(persisted, installationState);
  return guard.allowed ? persisted : null;
}

export function persistSetupScroll(y: number): void {
  try {
    sessionStorage.setItem(SCROLL_KEY, String(Math.max(0, Math.round(y))));
  } catch {
    /* ignore */
  }
}

export function readPersistedSetupScroll(): number | null {
  try {
    const raw = sessionStorage.getItem(SCROLL_KEY);
    if (raw == null) return null;
    const y = Number(raw);
    return Number.isFinite(y) ? y : null;
  } catch {
    return null;
  }
}

export function restoreSetupScroll(): void {
  const y = readPersistedSetupScroll();
  if (y != null) {
    window.requestAnimationFrame(() => window.scrollTo({ top: y, behavior: "auto" }));
  }
}
