#include "AuthRole.h"

const char *authRoleLabel(AuthRole role) {
  switch (role) {
    case AuthRole::Owner:
      return "owner";
    case AuthRole::Operator:
      return "operator";
    default:
      return "none";
  }
}

AuthRole parseAuthRole(const char *label) {
  if (!label) return AuthRole::None;
  if (strcmp(label, "owner") == 0) return AuthRole::Owner;
  if (strcmp(label, "operator") == 0) return AuthRole::Operator;
  return AuthRole::None;
}

bool authRoleAtLeast(AuthRole actual, AuthRole required) {
  return static_cast<uint8_t>(actual) >= static_cast<uint8_t>(required);
}
