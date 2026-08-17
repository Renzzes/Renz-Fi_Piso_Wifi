#pragma once

#include <Arduino.h>

#include "Config.h"

// Production RouterOS / existing-network scan logging.
// When RENZFI_VERBOSE_ROUTER_API=0 (default), firmware prints only milestone
// lines: LOGIN SUCCESS, SCAN COMPLETE, ADOPTION COMPLETE, and FAILURE.
// When RENZFI_VERBOSE_ROUTER_API=1, detailed diagnostics are enabled.

#if RENZFI_VERBOSE_ROUTER_API
#define RENZFI_ROUTER_API_VERBOSE(...) Serial.printf(__VA_ARGS__)
#define RENZFI_ROUTER_API_VERBOSE_LINE(msg) Serial.println(msg)
#else
#define RENZFI_ROUTER_API_VERBOSE(...) ((void)0)
#define RENZFI_ROUTER_API_VERBOSE_LINE(msg) ((void)0)
#endif

#define RENZFI_ROUTER_API_LOGIN_SUCCESS() Serial.println(F("[router-api] LOGIN SUCCESS"))
#define RENZFI_ROUTER_API_SCAN_COMPLETE() Serial.println(F("[existing-scan] SCAN COMPLETE"))
#define RENZFI_ROUTER_API_ADOPTION_COMPLETE() \
  Serial.println(F("[setup] ADOPTION COMPLETE"))
#define RENZFI_ROUTER_API_FAILURE(tag, code, msg) \
  Serial.printf("[router-api] FAILURE tag=%s code=%s msg=%s\n", (tag), (code), (msg))
