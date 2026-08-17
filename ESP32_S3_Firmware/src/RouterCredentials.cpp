#include "RouterCredentials.h"

#include <mbedtls/sha256.h>

namespace RouterCredentials {

namespace {

String bytesToHexPrefix(const uint8_t *data, size_t len) {
  static const char *hex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += hex[(data[i] >> 4) & 0x0F];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

size_t encodedCipherLen(const CredentialProtector::ProtectedSecretParts &parts) {
  return parts.cipherLen * 2;
}

}  // namespace

String fingerprintSecret(const String &secret) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx,
                        reinterpret_cast<const uint8_t *>(secret.c_str()),
                        secret.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  return bytesToHexPrefix(hash, 4);
}

bool secretsEqual(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); ++i) {
    diff |= static_cast<uint8_t>(a.charAt(i) ^ b.charAt(i));
  }
  return diff == 0;
}

void logSafeDiagnostics(const char *sourceLabel, const String &secret) {
  Serial.printf("[router-credentials] source=%s\n",
                sourceLabel ? sourceLabel : "unknown");
  Serial.printf("[router-credentials] length=%u\n",
                static_cast<unsigned>(secret.length()));
  Serial.printf("[router-credentials] fingerprint=%s\n",
                fingerprintSecret(secret).c_str());
}

void logStageBeforeProtect(const String &secret) {
  Serial.printf("[router-credentials] stage=before-protect length=%u fingerprint=%s\n",
                static_cast<unsigned>(secret.length()),
                fingerprintSecret(secret).c_str());
}

void logStageAfterProtect(const CredentialProtector::ProtectedSecretParts &parts) {
  Serial.printf(
      "[router-credentials] stage=after-protect encodedCipherLen=%u nonceLen=%u "
      "tagLen=%u\n",
      static_cast<unsigned>(encodedCipherLen(parts)),
      static_cast<unsigned>(parts.nonceLen),
      static_cast<unsigned>(parts.tagLen));
}

void logStageAfterWrite(size_t fileBytes) {
  Serial.printf("[router-credentials] stage=after-write fileBytes=%u\n",
                static_cast<unsigned>(fileBytes));
}

void logStageAfterRead(const CredentialProtector::ProtectedSecretParts &parts) {
  Serial.printf(
      "[router-credentials] stage=after-read encodedCipherLen=%u nonceLen=%u "
      "tagLen=%u\n",
      static_cast<unsigned>(encodedCipherLen(parts)),
      static_cast<unsigned>(parts.nonceLen),
      static_cast<unsigned>(parts.tagLen));
}

void logStageAfterUnprotect(const String &secret) {
  Serial.printf("[router-credentials] stage=after-unprotect length=%u fingerprint=%s\n",
                static_cast<unsigned>(secret.length()),
                fingerprintSecret(secret).c_str());
}

void logRoundtrip(bool ok) {
  Serial.printf("[router-credentials] roundtrip=%s\n", ok ? "ok" : "mismatch");
}

bool verifyRouterCredentialRoundTrip(const String &originalPassword,
                                       const String &protectedBlobFromStorage,
                                       String &failureStage) {
  failureStage = "";

  CredentialProtector::ProtectedSecretParts parts;
  if (!CredentialProtector::describeProtectedBlob(protectedBlobFromStorage, parts)) {
    failureStage = "decode";
    logRoundtrip(false);
    return false;
  }
  logStageAfterRead(parts);

  String recovered;
  if (!CredentialProtector::unprotectSecret(protectedBlobFromStorage, recovered,
                                            &parts)) {
    failureStage = "decrypt";
    logRoundtrip(false);
    return false;
  }
  logStageAfterUnprotect(recovered);

  if (originalPassword.length() != recovered.length() ||
      !secretsEqual(originalPassword, recovered)) {
    failureStage = "compare";
    logRoundtrip(false);
    return false;
  }

  logRoundtrip(true);
  return true;
}

}  // namespace RouterCredentials
