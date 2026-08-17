#pragma once

#include <Arduino.h>

enum class AuthRole : uint8_t {
  None = 0,
  Operator = 1,
  Owner = 2,
};

const char *authRoleLabel(AuthRole role);
AuthRole parseAuthRole(const char *label);
bool authRoleAtLeast(AuthRole actual, AuthRole required);
