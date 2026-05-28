export type ApiErrorCode =
  | "BAD_REQUEST"
  | "UNAUTHORIZED"
  | "FORBIDDEN"
  | "NOT_FOUND"
  | "RATE_LIMITED"
  | "LOGIN_RATE_LIMITED"
  | "LOCKED"
  | "NO_PASSWORD"
  | "INVALID"
  | "CSRF"
  | "IP_NOT_ALLOWED"
  | "SUBNET_NOT_ALLOWED"
  | "INTERNAL_ERROR"
  | "UPSTREAM_UNAVAILABLE";

export class ApiError extends Error {
  constructor(
    message: string,
    public status: number,
    public code: ApiErrorCode = "INTERNAL_ERROR",
  ) {
    super(message);
    this.name = "ApiError";
  }
}
