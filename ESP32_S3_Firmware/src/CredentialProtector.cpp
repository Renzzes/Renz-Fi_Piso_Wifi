#include "CredentialProtector.h"

#include <ESP.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>

namespace {

constexpr const char kPrefixV1[] = "enc:v1:";
constexpr const char kPrefixV2[] = "enc:v2:";

bool deriveKey(uint8_t outKey[16]) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);

  const uint64_t mac = ESP.getEfuseMac();
  const uint8_t *macBytes = reinterpret_cast<const uint8_t *>(&mac);
  mbedtls_sha256_update(&ctx, macBytes, sizeof(mac));
  mbedtls_sha256_update(
      &ctx, reinterpret_cast<const uint8_t *>("renzfi-cred-protect-v1"),
      strlen("renzfi-cred-protect-v1"));
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  memcpy(outKey, hash, 16);
  return true;
}

String bytesToHex(const uint8_t *data, size_t len) {
  static const char *hex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += hex[(data[i] >> 4) & 0x0F];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

String u32ToHex(uint32_t value) {
  uint8_t bytes[4] = {
      static_cast<uint8_t>((value >> 24) & 0xFF),
      static_cast<uint8_t>((value >> 16) & 0xFF),
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>(value & 0xFF),
  };
  return bytesToHex(bytes, sizeof(bytes));
}

bool hexToBytes(const String &hex, uint8_t *out, size_t outMax, size_t &written) {
  written = 0;
  if (hex.length() == 0 || (hex.length() % 2) != 0) return false;
  for (size_t i = 0; i + 1 < hex.length() && written < outMax; i += 2) {
    char buf[3] = {hex.charAt(i), hex.charAt(i + 1), '\0'};
    out[written++] = static_cast<uint8_t>(strtoul(buf, nullptr, 16));
  }
  return written > 0;
}

bool hexToU32(const String &hex, uint32_t &valueOut) {
  uint8_t bytes[4];
  size_t written = 0;
  if (!hexToBytes(hex, bytes, sizeof(bytes), written) || written != sizeof(bytes)) {
    return false;
  }
  valueOut = (static_cast<uint32_t>(bytes[0]) << 24) |
             (static_cast<uint32_t>(bytes[1]) << 16) |
             (static_cast<uint32_t>(bytes[2]) << 8) |
             static_cast<uint32_t>(bytes[3]);
  return true;
}

void assignBytes(String &out, const uint8_t *data, size_t len) {
  out = "";
  if (len == 0) return;
  if (!out.reserve(len)) return;
  for (size_t i = 0; i < len; ++i) {
    out += static_cast<char>(data[i]);
  }
}

bool ctrCrypt(const uint8_t key[16], const uint8_t initialNonce[16],
              const uint8_t *input, uint8_t *output, size_t len) {
  uint8_t nonce[16];
  memcpy(nonce, initialNonce, sizeof(nonce));
  uint8_t streamBlock[16]{};
  size_t ncOffset = 0;

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_enc(&aes, key, 128) != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }

  const int rc =
      mbedtls_aes_crypt_ctr(&aes, len, &ncOffset, nonce, streamBlock, input, output);
  mbedtls_aes_free(&aes);
  return rc == 0;
}

void fillParts(CredentialProtector::ProtectedSecretParts *partsOut,
               size_t nonceLen, size_t plainLen, size_t cipherLen) {
  if (!partsOut) return;
  partsOut->nonceLen  = nonceLen;
  partsOut->plainLen  = plainLen;
  partsOut->cipherLen = cipherLen;
  partsOut->tagLen    = 0;
}

bool protectV2(const String &plaintext, String &outProtected,
               CredentialProtector::ProtectedSecretParts *partsOut) {
  const size_t plainLen = plaintext.length();
  if (plainLen == 0 || plainLen > 4096) return false;

  uint8_t key[16];
  if (!deriveKey(key)) return false;

  uint8_t initialNonce[16];
  esp_fill_random(initialNonce, sizeof(initialNonce));

  uint8_t *cipher = static_cast<uint8_t *>(malloc(plainLen));
  if (!cipher) return false;

  if (!ctrCrypt(key, initialNonce,
                reinterpret_cast<const uint8_t *>(plaintext.c_str()), cipher,
                plainLen)) {
    free(cipher);
    return false;
  }

  outProtected = String(kPrefixV2) + bytesToHex(initialNonce, sizeof(initialNonce)) +
                 ":" + u32ToHex(static_cast<uint32_t>(plainLen)) + ":" +
                 bytesToHex(cipher, plainLen);
  free(cipher);

  fillParts(partsOut, sizeof(initialNonce), plainLen, plainLen);
  return true;
}

bool unprotectV2(const String &protectedBlob, String &outPlaintext,
                 CredentialProtector::ProtectedSecretParts *partsOut) {
  outPlaintext = "";

  const size_t prefixLen = strlen(kPrefixV2);
  if (!protectedBlob.startsWith(kPrefixV2)) return false;

  int colon1 = protectedBlob.indexOf(':', prefixLen);
  if (colon1 < 0) return false;
  int colon2 = protectedBlob.indexOf(':', colon1 + 1);
  if (colon2 < 0) return false;

  const String nonceHex = protectedBlob.substring(prefixLen, colon1);
  const String plainLenHex = protectedBlob.substring(colon1 + 1, colon2);
  const String cipherHex = protectedBlob.substring(colon2 + 1);

  if (nonceHex.length() != 32 || plainLenHex.length() != 8) return false;

  uint8_t initialNonce[16];
  size_t nonceWritten = 0;
  if (!hexToBytes(nonceHex, initialNonce, sizeof(initialNonce), nonceWritten) ||
      nonceWritten != sizeof(initialNonce)) {
    return false;
  }

  uint32_t plainLenU32 = 0;
  if (!hexToU32(plainLenHex, plainLenU32) || plainLenU32 == 0 ||
      plainLenU32 > 4096) {
    return false;
  }
  const size_t plainLen = plainLenU32;

  if (cipherHex.length() != plainLen * 2) return false;

  uint8_t *cipher = static_cast<uint8_t *>(malloc(plainLen));
  uint8_t *plain  = static_cast<uint8_t *>(malloc(plainLen));
  if (!cipher || !plain) {
    free(cipher);
    free(plain);
    return false;
  }

  size_t cipherWritten = 0;
  if (!hexToBytes(cipherHex, cipher, plainLen, cipherWritten) ||
      cipherWritten != plainLen) {
    free(cipher);
    free(plain);
    return false;
  }

  uint8_t key[16];
  if (!deriveKey(key)) {
    free(cipher);
    free(plain);
    return false;
  }

  if (!ctrCrypt(key, initialNonce, cipher, plain, plainLen)) {
    free(cipher);
    free(plain);
    return false;
  }
  free(cipher);

  assignBytes(outPlaintext, plain, plainLen);
  free(plain);

  fillParts(partsOut, sizeof(initialNonce), plainLen, plainLen);
  return outPlaintext.length() == plainLen;
}

// Legacy enc:v1 blobs stored the AES-CTR counter block AFTER encryption,
// which makes decryption nondeterministic. Refuse to decode them.
bool unprotectV1(const String &protectedBlob, String &outPlaintext) {
  (void)protectedBlob;
  outPlaintext = "";
  return false;
}

bool describeProtectedBlobV2(const String &protectedBlob,
                             CredentialProtector::ProtectedSecretParts &partsOut) {
  partsOut = {};
  const size_t prefixLen = strlen(kPrefixV2);
  if (!protectedBlob.startsWith(kPrefixV2)) return false;

  int colon1 = protectedBlob.indexOf(':', prefixLen);
  if (colon1 < 0) return false;
  int colon2 = protectedBlob.indexOf(':', colon1 + 1);
  if (colon2 < 0) return false;

  const String nonceHex = protectedBlob.substring(prefixLen, colon1);
  const String plainLenHex = protectedBlob.substring(colon1 + 1, colon2);
  const String cipherHex = protectedBlob.substring(colon2 + 1);

  if (nonceHex.length() != 32 || plainLenHex.length() != 8) return false;

  uint32_t plainLenU32 = 0;
  if (!hexToU32(plainLenHex, plainLenU32) || plainLenU32 == 0 ||
      plainLenU32 > 4096) {
    return false;
  }

  if (cipherHex.length() != plainLenU32 * 2) return false;

  fillParts(&partsOut, 16, plainLenU32, plainLenU32);
  return true;
}

}  // namespace

namespace CredentialProtector {

bool protectSecret(const String &plaintext, String &outProtected,
                   ProtectedSecretParts *partsOut) {
  return protectV2(plaintext, outProtected, partsOut);
}

bool describeProtectedBlob(const String &protectedBlob,
                           ProtectedSecretParts &partsOut) {
  partsOut = {};
  if (protectedBlob.startsWith(kPrefixV2)) {
    return describeProtectedBlobV2(protectedBlob, partsOut);
  }
  return false;
}

bool unprotectSecret(const String &protectedBlob, String &outPlaintext,
                     ProtectedSecretParts *partsOut) {
  outPlaintext = "";
  if (protectedBlob.startsWith(kPrefixV2)) {
    return unprotectV2(protectedBlob, outPlaintext, partsOut);
  }
  if (protectedBlob.startsWith(kPrefixV1)) {
    return unprotectV1(protectedBlob, outPlaintext);
  }
  return false;
}

}  // namespace CredentialProtector
