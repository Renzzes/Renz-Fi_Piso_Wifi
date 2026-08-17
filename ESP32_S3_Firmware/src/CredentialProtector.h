#pragma once

#include <Arduino.h>

namespace CredentialProtector {

struct ProtectedSecretParts {
  size_t nonceLen = 0;
  size_t plainLen = 0;
  size_t cipherLen = 0;
  size_t tagLen = 0;
};

bool protectSecret(const String &plaintext, String &outProtected,
                   ProtectedSecretParts *partsOut = nullptr);
bool unprotectSecret(const String &protectedBlob, String &outPlaintext,
                     ProtectedSecretParts *partsOut = nullptr);
bool describeProtectedBlob(const String &protectedBlob, ProtectedSecretParts &partsOut);

}  // namespace CredentialProtector
