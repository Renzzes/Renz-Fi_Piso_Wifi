import { isApiError, isNetworkError } from "@/services/api";

/** Consistent user-facing error text for setup wizard async actions. */
export function setupErrorMessage(error: unknown, fallback: string): string {
  if (isNetworkError(error)) return error.message;
  if (isApiError(error)) return error.message;
  return fallback;
}
