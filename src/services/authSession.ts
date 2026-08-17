let unauthorizedHandler: (() => void) | null = null;
let handlingUnauthorized = false;

export function setUnauthorizedHandler(handler: (() => void) | null) {
  unauthorizedHandler = handler;
}

export function handleUnauthorizedResponse(path: string) {
  if (handlingUnauthorized) return;
  if (path.includes("/auth/login")) return;
  if (path.includes("/api/system/factory-reset")) return;

  if (!unauthorizedHandler) return;

  handlingUnauthorized = true;
  try {
    unauthorizedHandler();
  } finally {
    handlingUnauthorized = false;
  }
}
