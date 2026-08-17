#pragma once

#include <Arduino.h>

#include "CredentialProtector.h"

// Safe credential helpers for RouterOS API jobs. Never logs secrets.
namespace RouterCredentials {

String fingerprintSecret(const String &secret);
bool secretsEqual(const String &a, const String &b);
void logSafeDiagnostics(const char *sourceLabel, const String &secret);

void logStageBeforeProtect(const String &secret);
void logStageAfterProtect(const CredentialProtector::ProtectedSecretParts &parts);
void logStageAfterWrite(size_t fileBytes);
void logStageAfterRead(const CredentialProtector::ProtectedSecretParts &parts);
void logStageAfterUnprotect(const String &secret);
void logRoundtrip(bool ok);

bool verifyRouterCredentialRoundTrip(const String &originalPassword,
                                       const String &protectedBlobFromStorage,
                                       String &failureStage);

}  // namespace RouterCredentials
