import helmet from "helmet";

export function securityMiddleware() {
  // Disable CSP to avoid breakage with the current SPA shell.
  return helmet({
    contentSecurityPolicy: false,
    crossOriginEmbedderPolicy: false,
  });
}
