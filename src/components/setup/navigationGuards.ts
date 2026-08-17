import {
  maxAllowedScreenIndex,
  screenIndex,
  type SetupScreenId,
} from "@/components/setup/stepRouter";
import type { InstallationState } from "@/types/provisioning";

export type NavigationGuardResult = {
  allowed: boolean;
  reason?: string;
};

/** Prevent skipping ahead of backend installation progress. */
export function canNavigateForward(
  currentScreen: SetupScreenId,
  targetScreen: SetupScreenId,
  installationState: InstallationState,
): NavigationGuardResult {
  const targetIdx = screenIndex(targetScreen);
  const maxIdx = maxAllowedScreenIndex(installationState);

  if (targetIdx > maxIdx) {
    return {
      allowed: false,
      reason: "Complete the current step before continuing.",
    };
  }

  if (installationState === "ready" && targetScreen !== "complete") {
    return {
      allowed: false,
      reason: "Installation is already complete.",
    };
  }

  if (targetIdx < screenIndex(currentScreen)) {
    return { allowed: true };
  }

  if (targetIdx <= maxIdx + 1 && targetIdx === screenIndex(currentScreen) + 1) {
    return { allowed: true };
  }

  if (targetIdx <= maxIdx) {
    return { allowed: true };
  }

  return {
    allowed: false,
    reason: "This step is not available yet.",
  };
}

/** Back navigation is UI-only; always allowed unless at first screen or after ready. */
export function canNavigateBack(
  currentScreen: SetupScreenId,
  installationState: InstallationState,
): NavigationGuardResult {
  if (installationState === "ready") {
    return {
      allowed: false,
      reason: "Installation is complete. Return to the dashboard.",
    };
  }

  if (screenIndex(currentScreen) <= 0) {
    return {
      allowed: false,
      reason: "Already at the first step.",
    };
  }

  return { allowed: true };
}

export function canNavigateToScreen(
  targetScreen: SetupScreenId,
  installationState: InstallationState,
): NavigationGuardResult {
  if (installationState === "ready" && targetScreen !== "complete") {
    return {
      allowed: false,
      reason: "Installation is already complete.",
    };
  }

  const targetIdx = screenIndex(targetScreen);
  const maxIdx = maxAllowedScreenIndex(installationState);

  if (targetIdx > maxIdx) {
    return {
      allowed: false,
      reason: "This step is not available yet.",
    };
  }

  return { allowed: true };
}
