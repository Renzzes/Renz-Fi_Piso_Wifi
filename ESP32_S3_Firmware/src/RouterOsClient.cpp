#include "RouterOsClient.h"

#include <ETH.h>
#include <mbedtls/md5.h>

#include "Config.h"

void RouterOsClient::setTimeouts(uint32_t connectMs, uint32_t ioMs) {
  _connectTimeoutMs = connectMs;
  _ioTimeoutMs      = ioMs;
}

void RouterOsClient::setCredentials(const String &host, const String &username,
                                    const String &password, uint16_t port) {
  _host      = host;
  _username  = username;
  _password  = password;
  _port      = port;
}

void RouterOsClient::setError(const String &message) {
  _lastError = message;
}

bool RouterOsClient::writeWord(const String &word) {
  const size_t len = word.length();
  if (len < 0x80) {
    if (_client.write((uint8_t)len) != 1) return false;
  } else if (len < 0x4000) {
    if (_client.write((uint8_t)((len >> 8) | 0x80)) != 1) return false;
    if (_client.write((uint8_t)(len & 0xFF)) != 1) return false;
  } else if (len < 0x200000) {
    if (_client.write((uint8_t)((len >> 16) | 0xC0)) != 1) return false;
    if (_client.write((uint8_t)((len >> 8) & 0xFF)) != 1) return false;
    if (_client.write((uint8_t)(len & 0xFF)) != 1) return false;
  } else {
    return false;
  }

  return _client.write((const uint8_t *)word.c_str(), len) == len;
}

bool RouterOsClient::endSentence() {
  return _client.write((uint8_t)0) == 1;
}

String RouterOsClient::bytesToHex(const uint8_t *data, size_t len) {
  static const char *hex = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out += hex[(data[i] >> 4) & 0x0F];
    out += hex[data[i] & 0x0F];
  }
  return out;
}

bool RouterOsClient::parseAttr(const String &word, String &key, String &value) {
  if (!word.startsWith("=")) return false;
  int eq = word.indexOf('=', 1);
  if (eq < 0) return false;
  key   = word.substring(1, eq);
  value = word.substring(eq + 1);
  return true;
}

bool RouterOsClient::writeSentence(const String *words, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (!writeWord(words[i])) {
      setError("Failed writing RouterOS API word");
      return false;
    }
  }
  return endSentence();
}

bool RouterOsClient::readByte(uint8_t &out) {
  const uint32_t deadline = millis() + _ioTimeoutMs;
  while (millis() < deadline) {
    if (_client.available() > 0) {
      out = (uint8_t)_client.read();
      return true;
    }
    delay(1);
  }
  setError("RouterOS API read timeout");
  return false;
}

bool RouterOsClient::readWord(String &out) {
  uint8_t b = 0;
  if (!readByte(b)) return false;

  size_t len = 0;
  if (b < 0x80) {
    len = b;
  } else if ((b & 0xC0) == 0x80) {
    uint8_t b2 = 0;
    if (!readByte(b2)) return false;
    len = ((size_t)(b & 0x3F) << 8) + b2;
  } else if ((b & 0xE0) == 0xC0) {
    uint8_t b2 = 0;
    uint8_t b3 = 0;
    if (!readByte(b2) || !readByte(b3)) return false;
    len = ((size_t)(b & 0x1F) << 16) + ((size_t)b2 << 8) + b3;
  } else {
    setError("Invalid RouterOS API length prefix");
    return false;
  }

  if (len == 0) {
    out = "";
    return true;
  }

  out.reserve(len);
  const uint32_t deadline = millis() + _ioTimeoutMs;
  while (out.length() < len) {
    while (_client.available() == 0) {
      if (millis() >= deadline) {
        setError("RouterOS API payload read timeout");
        return false;
      }
      delay(1);
    }
    out += (char)_client.read();
  }
  return true;
}

void RouterOsClient::addReplyRecord(CommandResult &out, const String *words,
                                    size_t wordCount) {
  if (out.replyCount >= MAX_REPLY_RECORDS) return;

  ReplyRecord &record = out.replies[out.replyCount++];
  record.attrCount    = 0;

  for (size_t i = 0; i < wordCount; ++i) {
    if (words[i] == "!re" || words[i] == "!trap" || words[i] == "!fatal") {
      continue;
    }
    if (record.attrCount < 12) {
      record.attrs[record.attrCount++] = words[i];
    }
  }
}

bool RouterOsClient::readSentence(CommandResult &out) {
  String words[MAX_SENTENCE_WORDS];
  size_t wordCount = 0;

  while (true) {
    String word;
    if (!readWord(word)) return false;

    if (word.length() == 0) {
      if (wordCount == 0) continue;
      break;
    }

    if (wordCount < MAX_SENTENCE_WORDS) {
      words[wordCount++] = word;
    }
  }

  if (wordCount == 0) return true;

  const String &type = words[0];
  if (type == "!re") {
    addReplyRecord(out, words, wordCount);
  } else if (type == "!done") {
    out.doneReceived = true;
  } else if (type == "!trap") {
    out.trapReceived = true;
    for (size_t i = 1; i < wordCount; ++i) {
      String key;
      String value;
      if (parseAttr(words[i], key, value) && key == "message") {
        out.trapMessage = value;
      }
    }
  } else if (type == "!fatal") {
    out.fatalReceived = true;
    for (size_t i = 1; i < wordCount; ++i) {
      String key;
      String value;
      if (parseAttr(words[i], key, value) && key == "message") {
        out.fatalMessage = value;
      }
    }
  }

  return true;
}

bool RouterOsClient::drainCommandResult(CommandResult &out) {
  while (!out.doneReceived && !out.trapReceived && !out.fatalReceived) {
    if (!readSentence(out)) return false;
  }
  return true;
}

bool RouterOsClient::connect() {
  disconnect();

  if (!ETH.connected() && !ETH.linkUp()) {
    setError("Ethernet link is down");
    return false;
  }
  if (_host.isEmpty()) {
    setError("MikroTik host is not configured");
    return false;
  }

  const uint32_t deadline = millis() + _connectTimeoutMs;
  while (!_client.connect(_host.c_str(), _port)) {
    if (millis() >= deadline) {
      setError("TCP connect to RouterOS API failed");
      return false;
    }
    delay(50);
  }

  _client.setTimeout(_ioTimeoutMs);
  _connected = true;
  _loggedIn  = false;
  _lastError = "";
  return true;
}

void RouterOsClient::disconnect() {
  if (_client.connected()) {
    _client.stop();
  }
  _connected = false;
  _loggedIn  = false;
}

bool RouterOsClient::sendLoginSentence(const String &nameWord,
                                       const String &credWord) {
  if (credWord.isEmpty()) {
    const String words[] = {"/login", nameWord};
    return writeSentence(words, 2);
  }
  const String words[] = {"/login", nameWord, credWord};
  return writeSentence(words, 3);
}

bool RouterOsClient::hexToBytes(const String &hex, uint8_t *out, size_t outMax,
                                size_t &written) {
  written = 0;
  String cleaned = hex;
  cleaned.trim();
  if (cleaned.length() == 0 || (cleaned.length() % 2) != 0) return false;

  for (size_t i = 0; i + 1 < cleaned.length() && written < outMax; i += 2) {
    char buf[3] = {cleaned[i], cleaned[i + 1], '\0'};
    out[written++] = (uint8_t)strtoul(buf, nullptr, 16);
  }
  return written > 0;
}

String RouterOsClient::md5RouterOsResponse(const String &password,
                                           const String &challengeHex) {
  uint8_t challenge[32];
  size_t challengeLen = 0;
  if (!hexToBytes(challengeHex, challenge, sizeof(challenge), challengeLen)) {
    return "";
  }

  uint8_t input[1 + 128 + 32];
  size_t inputLen = 0;
  input[inputLen++] = 0x00;
  const size_t passLen = password.length();
  if (passLen > 128) return "";
  memcpy(input + inputLen, password.c_str(), passLen);
  inputLen += passLen;
  memcpy(input + inputLen, challenge, challengeLen);
  inputLen += challengeLen;

  uint8_t digest[16];
  mbedtls_md5_context ctx;
  mbedtls_md5_init(&ctx);
#if defined(mbedtls_md5_starts_ret)
  mbedtls_md5_starts_ret(&ctx);
  mbedtls_md5_update_ret(&ctx, input, inputLen);
  mbedtls_md5_finish_ret(&ctx, digest);
#else
  mbedtls_md5_starts(&ctx);
  mbedtls_md5_update(&ctx, input, inputLen);
  mbedtls_md5_finish(&ctx, digest);
#endif
  mbedtls_md5_free(&ctx);

  return bytesToHex(digest, 16);
}

String RouterOsClient::challengeFromResult(const CommandResult &result) {
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    const ReplyRecord &record = result.replies[i];
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (parseAttr(record.attrs[j], key, value) && key == "ret" &&
          !value.isEmpty()) {
        return value;
      }
    }
  }
  return "";
}

bool RouterOsClient::loginWithPassword() {
  CommandResult result;
  if (!sendLoginSentence("=name=" + _username, "=password=" + _password)) {
    setError("Failed sending RouterOS login");
    return false;
  }
  if (!drainCommandResult(result)) return false;

  if (result.doneReceived) {
    _loggedIn = true;
    return true;
  }

  String challenge = challengeFromResult(result);
  if (!challenge.isEmpty()) {
    return loginWithChallenge(challenge);
  }

  if (result.trapReceived) {
    CommandResult challengeResult;
    if (sendLoginSentence("=name=" + _username, "") &&
        drainCommandResult(challengeResult)) {
      challenge = challengeFromResult(challengeResult);
      if (!challenge.isEmpty()) {
        return loginWithChallenge(challenge);
      }
    }
    setError(result.trapMessage.isEmpty() ? "RouterOS login failed"
                                            : result.trapMessage);
  } else {
    setError("RouterOS login incomplete");
  }

  return false;
}

bool RouterOsClient::loginWithChallenge(const String &challengeHex) {
  const String response = md5RouterOsResponse(_password, challengeHex);
  if (response.isEmpty()) {
    setError("Failed building RouterOS challenge response");
    return false;
  }

  CommandResult result;
  if (!sendLoginSentence("=name=" + _username, "=response=" + response)) {
    setError("Failed sending RouterOS challenge login");
    return false;
  }
  if (!drainCommandResult(result)) return false;

  if (result.doneReceived) {
    _loggedIn = true;
    return true;
  }

  if (result.trapReceived) {
    setError(result.trapMessage.isEmpty() ? "RouterOS challenge login failed"
                                            : result.trapMessage);
  } else if (result.fatalReceived) {
    setError(result.fatalMessage.isEmpty() ? "RouterOS fatal login error"
                                           : result.fatalMessage);
  } else {
    setError("RouterOS login incomplete");
  }
  return false;
}

bool RouterOsClient::login() {
  if (!_connected) {
    setError("Not connected to RouterOS API");
    return false;
  }
  if (_username.isEmpty()) {
    setError("RouterOS API username is not configured");
    return false;
  }

  _loggedIn = false;
  return loginWithPassword();
}

bool RouterOsClient::executeCommand(const String &commandPath,
                                    const String &attribute,
                                    CommandResult &out) {
  if (attribute.isEmpty()) {
    return executeCommand(commandPath, out);
  }
  const String attrs[] = {attribute};
  return executeCommand(commandPath, attrs, 1, out);
}

bool RouterOsClient::executeCommand(const String &commandPath,
                                    const String *attributes,
                                    size_t attributeCount,
                                    CommandResult &out) {
  if (!_connected) {
    setError("Not connected to RouterOS API");
    return false;
  }
  if (!_loggedIn) {
    setError("RouterOS API not authenticated");
    return false;
  }
  if (attributeCount + 1 > MAX_SENTENCE_WORDS) {
    setError("RouterOS command has too many attributes");
    return false;
  }

  out = CommandResult();

  String words[MAX_SENTENCE_WORDS];
  words[0] = commandPath;
  for (size_t i = 0; i < attributeCount; ++i) {
    words[i + 1] = attributes[i];
  }

  if (!writeSentence(words, 1 + attributeCount)) {
    setError("Failed sending RouterOS command");
    return false;
  }

  if (!drainCommandResult(out)) return false;

  if (out.fatalReceived) {
    setError(out.fatalMessage.isEmpty() ? "RouterOS fatal error" : out.fatalMessage);
    return false;
  }

  return true;
}

bool RouterOsClient::executeCommand(const String &commandPath,
                                    CommandResult &out) {
  if (!_connected) {
    setError("Not connected to RouterOS API");
    return false;
  }
  if (!_loggedIn) {
    setError("RouterOS API not authenticated");
    return false;
  }

  out = CommandResult();

  const String words[] = {commandPath};
  if (!writeSentence(words, 1)) {
    setError("Failed sending RouterOS command");
    return false;
  }

  if (!drainCommandResult(out)) return false;

  if (out.fatalReceived) {
    setError(out.fatalMessage.isEmpty() ? "RouterOS fatal error" : out.fatalMessage);
    return false;
  }

  return true;
}
