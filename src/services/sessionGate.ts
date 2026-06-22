import { authApi } from "./auth";

const RELOGIN_REQUIRED_KEY = "renz_admin_relogin_required";
const LOGOUT_RETRY_MS = 3000;

export type ReloginReason = "reconnect" | "disconnect";

type ReloginRecord = {
  at: number;
  reason: ReloginReason;
};

export function markReloginRequired(reason: ReloginReason = "reconnect"): void {
  try {
    const record: ReloginRecord = { at: Date.now(), reason };
    localStorage.setItem(RELOGIN_REQUIRED_KEY, JSON.stringify(record));
  } catch {
    // ignore storage failures
  }
}

export function isReloginRequired(): boolean {
  try {
    return localStorage.getItem(RELOGIN_REQUIRED_KEY) !== null;
  } catch {
    return false;
  }
}

export function clearReloginRequired(): void {
  try {
    localStorage.removeItem(RELOGIN_REQUIRED_KEY);
  } catch {
    // ignore storage failures
  }
}

/**
 * Maps /api/health session.authenticated to client auth state.
 * When relogin is required, stale HttpOnly cookies must not restore the dashboard.
 */
export function resolveBootstrapAuth(serverAuthenticated: boolean): boolean {
  if (!isReloginRequired()) return serverAuthenticated;
  if (!serverAuthenticated) {
    clearReloginRequired();
    return false;
  }
  return false;
}

export async function logoutBestEffort(maxAttempts = 5): Promise<boolean> {
  for (let attempt = 0; attempt < maxAttempts; attempt++) {
    try {
      await authApi.logout();
      return true;
    } catch {
      if (attempt < maxAttempts - 1) {
        await new Promise((resolve) => window.setTimeout(resolve, 500 * (attempt + 1)));
      }
    }
  }
  return false;
}

let logoutRetryTimer: number | null = null;

export function startLogoutRetryUntilSuccess(): void {
  stopLogoutRetry();

  const tick = async () => {
    if (!isReloginRequired()) {
      stopLogoutRetry();
      return;
    }

    const cleared = await logoutBestEffort(1);
    if (cleared) {
      stopLogoutRetry();
      return;
    }

    logoutRetryTimer = window.setTimeout(() => void tick(), LOGOUT_RETRY_MS);
  };

  void tick();
}

export function stopLogoutRetry(): void {
  if (logoutRetryTimer !== null) {
    window.clearTimeout(logoutRetryTimer);
    logoutRetryTimer = null;
  }
}
