#include "RouterOsClient.h"

#include <ETH.h>
#include <mbedtls/md5.h>
#include <memory>

#include "Config.h"
#include "DmaMemoryMonitor.h"
#include "FinishTrace.h"
#include "RenzFiRouterApiLog.h"
#include "RouterApiTransportGate.h"
#include "RouterWorkerDiagnostics.h"

namespace {

SemaphoreHandle_t &ioMutexHandle() {
  static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutex();
  return handle;
}

}  // namespace

RouterOsClient::ReplyRecord::ReplyRecord(ReplyRecord &&other) noexcept
    : overflowAttrs(other.overflowAttrs),
      overflowCapacity(other.overflowCapacity),
      attrCount(other.attrCount) {
  for (uint8_t i = 0; i < INLINE_ATTRS; ++i) {
    inlineAttrs[i] = std::move(other.inlineAttrs[i]);
  }
  other.overflowAttrs     = nullptr;
  other.overflowCapacity  = 0;
  other.attrCount         = 0;
}

RouterOsClient::ReplyRecord &RouterOsClient::ReplyRecord::operator=(
    ReplyRecord &&other) noexcept {
  if (this == &other) return *this;
  clearKeepCapacity();
  freeOverflow();
  for (uint8_t i = 0; i < INLINE_ATTRS; ++i) {
    inlineAttrs[i] = std::move(other.inlineAttrs[i]);
  }
  overflowAttrs     = other.overflowAttrs;
  overflowCapacity  = other.overflowCapacity;
  attrCount         = other.attrCount;
  other.overflowAttrs     = nullptr;
  other.overflowCapacity  = 0;
  other.attrCount         = 0;
  return *this;
}

void RouterOsClient::ReplyRecord::freeOverflow() {
  delete[] overflowAttrs;
  overflowAttrs    = nullptr;
  overflowCapacity = 0;
}

void RouterOsClient::ReplyRecord::clearKeepCapacity() {
  for (uint8_t i = 0; i < attrCount && i < INLINE_ATTRS; ++i) {
    inlineAttrs[i] = "";
  }
  if (overflowAttrs) {
    const uint8_t overflowUsed =
        attrCount > INLINE_ATTRS ? attrCount - INLINE_ATTRS : 0;
    for (uint8_t i = 0; i < overflowUsed && i < overflowCapacity; ++i) {
      overflowAttrs[i] = "";
    }
  }
  attrCount = 0;
}

const String &RouterOsClient::ReplyRecord::attr(uint8_t index) const {
  static const String kEmpty;
  if (index >= attrCount) return kEmpty;
  if (index < INLINE_ATTRS) return inlineAttrs[index];
  const uint8_t overflowIdx = index - INLINE_ATTRS;
  if (overflowAttrs && overflowIdx < overflowCapacity) {
    return overflowAttrs[overflowIdx];
  }
  return kEmpty;
}

bool RouterOsClient::ReplyRecord::addAttr(String word) {
  if (attrCount >= MAX_ATTRS) return false;
  if (attrCount < INLINE_ATTRS) {
    inlineAttrs[attrCount++] = std::move(word);
    return true;
  }

  const uint8_t overflowIdx = attrCount - INLINE_ATTRS;
  if (overflowIdx >= overflowCapacity) {
    uint8_t newCap = overflowCapacity == 0 ? 4 : static_cast<uint8_t>(overflowCapacity + 4);
    if (newCap > MAX_ATTRS - INLINE_ATTRS) {
      newCap = MAX_ATTRS - INLINE_ATTRS;
    }
    String *next = new (std::nothrow) String[newCap];
    if (!next) return false;
    for (uint8_t i = 0; i < overflowCapacity; ++i) {
      next[i] = std::move(overflowAttrs[i]);
    }
    delete[] overflowAttrs;
    overflowAttrs    = next;
    overflowCapacity = newCap;
  }
  overflowAttrs[overflowIdx] = std::move(word);
  ++attrCount;
  return true;
}

RouterOsClient::CommandResult::CommandResult(CommandResult &&other) noexcept
    : doneReceived(other.doneReceived),
      trapReceived(other.trapReceived),
      fatalReceived(other.fatalReceived),
      replyLimitReached(other.replyLimitReached),
      trapMessage(std::move(other.trapMessage)),
      trapCategory(std::move(other.trapCategory)),
      fatalMessage(std::move(other.fatalMessage)),
      fatalCategory(std::move(other.fatalCategory)),
      overflowReplies(other.overflowReplies),
      overflowReplyCapacity(other.overflowReplyCapacity),
      replyCount(other.replyCount) {
  for (uint8_t i = 0; i < INLINE_REPLIES; ++i) {
    inlineReplies[i] = std::move(other.inlineReplies[i]);
  }
  other.overflowReplies       = nullptr;
  other.overflowReplyCapacity = 0;
  other.replyCount            = 0;
}

RouterOsClient::CommandResult &RouterOsClient::CommandResult::operator=(
    CommandResult &&other) noexcept {
  if (this == &other) return *this;
  clearKeepCapacity();
  freeOverflow();
  doneReceived      = other.doneReceived;
  trapReceived      = other.trapReceived;
  fatalReceived     = other.fatalReceived;
  replyLimitReached = other.replyLimitReached;
  trapMessage       = std::move(other.trapMessage);
  trapCategory      = std::move(other.trapCategory);
  fatalMessage      = std::move(other.fatalMessage);
  fatalCategory     = std::move(other.fatalCategory);
  for (uint8_t i = 0; i < INLINE_REPLIES; ++i) {
    inlineReplies[i] = std::move(other.inlineReplies[i]);
  }
  overflowReplies       = other.overflowReplies;
  overflowReplyCapacity = other.overflowReplyCapacity;
  replyCount            = other.replyCount;
  other.overflowReplies       = nullptr;
  other.overflowReplyCapacity = 0;
  other.replyCount            = 0;
  return *this;
}

void RouterOsClient::CommandResult::freeOverflow() {
  delete[] overflowReplies;
  overflowReplies       = nullptr;
  overflowReplyCapacity = 0;
}

void RouterOsClient::CommandResult::clearKeepCapacity() {
  for (uint8_t i = 0; i < replyCount && i < INLINE_REPLIES; ++i) {
    inlineReplies[i].clearKeepCapacity();
  }
  if (overflowReplies) {
    const uint8_t overflowUsed =
        replyCount > INLINE_REPLIES ? replyCount - INLINE_REPLIES : 0;
    for (uint8_t i = 0; i < overflowUsed && i < overflowReplyCapacity; ++i) {
      overflowReplies[i].clearKeepCapacity();
    }
  }
  replyCount = 0;
}

RouterOsClient::ReplyRecord &RouterOsClient::CommandResult::replyAt(uint8_t index) {
  static ReplyRecord kFallback;
  if (index >= replyCount) return kFallback;
  if (index < INLINE_REPLIES) return inlineReplies[index];
  const uint8_t overflowIdx = index - INLINE_REPLIES;
  if (overflowReplies && overflowIdx < overflowReplyCapacity) {
    return overflowReplies[overflowIdx];
  }
  return kFallback;
}

const RouterOsClient::ReplyRecord &RouterOsClient::CommandResult::replyAt(
    uint8_t index) const {
  return const_cast<CommandResult *>(this)->replyAt(index);
}

RouterOsClient::ReplyRecord &RouterOsClient::CommandResult::appendReply() {
  // Discard sink used when we cannot allocate a new reply slot. Attributes
  // written here must never touch live reply storage (avoids corrupting prior
  // records or dereferencing a failed allocation).
  static ReplyRecord discardSink;

  if (replyCount >= MAX_REPLY_RECORDS) {
    replyLimitReached = true;
    discardSink.clearKeepCapacity();
    return discardSink;
  }

  if (replyCount < INLINE_REPLIES) {
    ReplyRecord &record = inlineReplies[replyCount++];
    record.clearKeepCapacity();
    return record;
  }

  const uint8_t overflowIdx = replyCount - INLINE_REPLIES;
  if (overflowIdx >= overflowReplyCapacity) {
    uint8_t newCap =
        overflowReplyCapacity == 0 ? 4 : static_cast<uint8_t>(overflowReplyCapacity + 4);
    if (newCap > MAX_REPLY_RECORDS - INLINE_REPLIES) {
      newCap = MAX_REPLY_RECORDS - INLINE_REPLIES;
    }
    ReplyRecord *next = new (std::nothrow) ReplyRecord[newCap];
    if (!next) {
      replyLimitReached = true;
      Serial.println(
          F("[router-api] reply alloc failed — truncating remaining replies"));
      discardSink.clearKeepCapacity();
      return discardSink;
    }
    for (uint8_t i = 0; i < overflowReplyCapacity; ++i) {
      next[i] = std::move(overflowReplies[i]);
    }
    delete[] overflowReplies;
    overflowReplies       = next;
    overflowReplyCapacity = newCap;
  }

  ReplyRecord &record = overflowReplies[overflowIdx];
  record.clearKeepCapacity();
  ++replyCount;
  return record;
}

RouterOsClient::IoLock::IoLock() {
  SemaphoreHandle_t mutex = ioMutexHandle();
  if (mutex && xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(20000)) == pdTRUE) {
    _held = true;
  } else {
    RENZFI_ROUTER_API_VERBOSE_LINE(
        "[router-api] WARNING: I/O mutex not acquired (proceeding unlocked)");
  }
}

RouterOsClient::IoLock::~IoLock() {
  if (_held) {
    SemaphoreHandle_t mutex = ioMutexHandle();
    if (mutex) xSemaphoreGiveRecursive(mutex);
  }
}

RouterOsClient::RouterOsClient() {
  _lastErrorCode = "ROUTEROS_API_UNAVAILABLE";
  RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] session allocated");
}

RouterOsClient::~RouterOsClient() { disconnect(); }

void RouterOsClient::setTimeouts(uint32_t connectMs, uint32_t ioMs) {
  _connectTimeoutMs  = connectMs;
  _sentenceTimeoutMs = ioMs;
}

bool RouterOsClient::jobExpired() const { return RouterApiTransportGate::jobExpired(); }

void RouterOsClient::setCredentials(const String &host, const String &username,
                                    const String &password, uint16_t port) {
  _host      = host;
  _username  = username;
  _password  = password;
  _port      = port;
}

void RouterOsClient::setCredentialSource(const char *source) {
  _credentialSource = source ? source : "";
}

void RouterOsClient::setCommandReplyLimits(uint8_t maxRecords,
                                           bool stopDrainOnLimit) {
  _replyRecordLimit =
      maxRecords > 0 && maxRecords <= MAX_REPLY_RECORDS ? maxRecords
                                                        : MAX_REPLY_RECORDS;
  _stopDrainOnReplyLimit = stopDrainOnLimit;
}

void RouterOsClient::resetCommandReplyLimits() {
  _replyRecordLimit       = MAX_REPLY_RECORDS;
  _stopDrainOnReplyLimit  = false;
}

void RouterOsClient::initializeCommandResult(CommandResult &out) {
  out.doneReceived      = false;
  out.trapReceived      = false;
  out.fatalReceived     = false;
  out.replyLimitReached = false;
  out.trapMessage       = "";
  out.trapCategory      = "";
  out.fatalMessage      = "";
  out.fatalCategory     = "";
  out.clearKeepCapacity();
}

void RouterOsClient::setError(const String &message, const char *code) {
  _lastError     = message;
  _lastErrorCode = code ? code : "ROUTEROS_API_UNAVAILABLE";
}

void RouterOsClient::resetCommandResult(CommandResult &out) {
  initializeCommandResult(out);
}

void RouterOsClient::flushReceiveBuffer() {
  while (_client.available() > 0) {
    _client.read();
  }
}

void RouterOsClient::appendTranscriptAttr(const String &key, const String &value) {
  if (!_transcriptAttrs) return;
  if (!_transcriptAttrs->isEmpty()) *_transcriptAttrs += "; ";
  *_transcriptAttrs += key + "=" + value;
}

void RouterOsClient::logParsedSentence(size_t wordCount, const char *context) {
  if (wordCount == 0) {
    RENZFI_ROUTER_API_VERBOSE("[router-api] %s sentence: (empty terminator)\n",
                              context);
    return;
  }

  const String &type = _sentenceWords[0];
  RENZFI_ROUTER_API_VERBOSE("[router-api] %s sentence type=%s words=%u\n", context,
                            type.c_str(), static_cast<unsigned>(wordCount));

  if (_transcriptTypes) {
    if (!_transcriptTypes->isEmpty()) *_transcriptTypes += ", ";
    *_transcriptTypes += type;
  }

  if (type == "!trap" || type == "!fatal") {
    for (size_t i = 1; i < wordCount; ++i) {
      String key;
      String value;
      if (parseAttr(_sentenceWords[i], key, value)) {
        RENZFI_ROUTER_API_VERBOSE("[router-api] %s attr %s=%s\n", context,
                                  key.c_str(), value.c_str());
      } else {
        // !fatal words are frequently plain text, not =key=value pairs.
        // Log the raw word so the real reason is always auditable.
        RENZFI_ROUTER_API_VERBOSE("[router-api] %s word=%s\n", context,
                                  _sentenceWords[i].c_str());
      }
    }
  } else if (type == "!re" && _transcriptAttrs) {
    for (size_t i = 1; i < wordCount; ++i) {
      String key;
      String value;
      if (parseAttr(_sentenceWords[i], key, value)) {
        appendTranscriptAttr(key, value);
      }
    }
  }
}

void RouterOsClient::applySentenceToResult(CommandResult &out, size_t wordCount) {
  if (wordCount == 0) return;

  const String &type = _sentenceWords[0];
  if (type == "!re") {
    addReplyRecord(out, _sentenceWords, wordCount);
  } else if (type == "!done") {
    out.doneReceived = true;
  } else if (type == "!trap") {
    out.trapReceived = true;
    for (size_t i = 1; i < wordCount; ++i) {
      String key;
      String value;
      if (parseAttr(_sentenceWords[i], key, value)) {
        if (key == "message") out.trapMessage = value;
        if (key == "category") out.trapCategory = value;
      }
    }
  } else if (type == "!fatal") {
    out.fatalReceived = true;
    for (size_t i = 1; i < wordCount; ++i) {
      String key;
      String value;
      if (parseAttr(_sentenceWords[i], key, value)) {
        if (key == "message") out.fatalMessage = value;
        if (key == "category") out.fatalCategory = value;
      } else if (out.fatalMessage.isEmpty()) {
        // Unlike !trap, RouterOS sends the !fatal reason as a bare word
        // (no =message= attribute prefix). Preserve it verbatim so the
        // real router-reported reason reaches the UI instead of a
        // generic fallback string.
        out.fatalMessage = _sentenceWords[i];
      }
    }
  }
}

size_t RouterOsClient::writeAndCapture(const uint8_t *data, size_t len) {
  const size_t written = _client.write(data, len);
  if (written != len) {
    DmaMemoryMonitor::logSnapshot("after tx");
  }
  if (_logLoginIo) {
    ++_loginTxWriteCalls;
    Serial.printf("[router-api] write() call=%u requested=%u returned=%u\n",
                  static_cast<unsigned>(_loginTxWriteCalls),
                  static_cast<unsigned>(len), static_cast<unsigned>(written));
    for (size_t i = 0; i < written && _loginTxCaptureLen < LOGIN_TX_CAPTURE_MAX; ++i) {
      _loginTxCapture[_loginTxCaptureLen++] = data[i];
    }
  }
  return written;
}

uint32_t RouterOsClient::crc32Of(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
  }
  return ~crc;
}

void RouterOsClient::dumpLoginTxCapture() const {
  if (!_logLoginIo) return;
  Serial.println("[router-api] TX RAW BYTES");
  Serial.println("[router-api] ---------------");
  char line[64];
  for (size_t i = 0; i < _loginTxCaptureLen; i += 16) {
    size_t pos = 0;
    for (size_t j = i; j < i + 16 && j < _loginTxCaptureLen; ++j) {
      pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", _loginTxCapture[j]);
    }
    Serial.printf("[router-api] TX: %s\n", line);
  }
  Serial.printf("[router-api] TX total bytes=%u\n",
                static_cast<unsigned>(_loginTxCaptureLen));
  Serial.printf("[router-api] TX write() calls=%u\n",
                static_cast<unsigned>(_loginTxWriteCalls));
  Serial.printf("[router-api] TX crc32=0x%08X\n",
                static_cast<unsigned>(crc32Of(_loginTxCapture, _loginTxCaptureLen)));
}

bool RouterOsClient::writeWord(const String &word) {
  const size_t len = word.length();
  if (len < 0x80) {
    const uint8_t b = (uint8_t)len;
    if (writeAndCapture(&b, 1) != 1) return false;
  } else if (len < 0x4000) {
    const uint8_t b1 = (uint8_t)((len >> 8) | 0x80);
    const uint8_t b2 = (uint8_t)(len & 0xFF);
    if (writeAndCapture(&b1, 1) != 1) return false;
    if (writeAndCapture(&b2, 1) != 1) return false;
  } else if (len < 0x200000) {
    const uint8_t b1 = (uint8_t)((len >> 16) | 0xC0);
    const uint8_t b2 = (uint8_t)((len >> 8) & 0xFF);
    const uint8_t b3 = (uint8_t)(len & 0xFF);
    if (writeAndCapture(&b1, 1) != 1) return false;
    if (writeAndCapture(&b2, 1) != 1) return false;
    if (writeAndCapture(&b3, 1) != 1) return false;
  } else {
    return false;
  }

  return writeAndCapture((const uint8_t *)word.c_str(), len) == len;
}

bool RouterOsClient::endSentence() {
  const uint8_t zero = 0;
  return writeAndCapture(&zero, 1) == 1;
}

size_t RouterOsClient::encodedWordSize(const String &word) {
  const size_t len = word.length();
  if (len < 0x80) return 1 + len;
  if (len < 0x4000) return 2 + len;
  if (len < 0x200000) return 3 + len;
  return 0;
}

size_t RouterOsClient::sentenceByteCount(const String *words, size_t count) {
  size_t total = 1;  // zero-length terminating word
  for (size_t i = 0; i < count; ++i) total += encodedWordSize(words[i]);
  return total;
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

String RouterOsClient::replyAttr(const CommandResult &result, uint8_t row,
                                 const char *keyName) {
  if (row >= result.replyCount || !keyName || !keyName[0]) return String();
  const auto &record = result.replyAt(row);
  for (uint8_t i = 0; i < record.attrCount; ++i) {
    String key;
    String value;
    if (parseAttr(record.attr(i), key, value) &&
        key.equalsIgnoreCase(keyName)) {
      return value;
    }
  }
  return String();
}

bool RouterOsClient::replyAttrToBuf(const CommandResult &result, uint8_t row,
                                    const char *keyName, char *out,
                                    size_t cap) {
  if (row >= result.replyCount || !keyName || !out || cap == 0) return false;
  const auto &record = result.replyAt(row);
  for (uint8_t i = 0; i < record.attrCount; ++i) {
    String key;
    String value;
    if (parseAttr(record.attr(i), key, value) &&
        key.equalsIgnoreCase(keyName)) {
      strncpy(out, value.c_str(), cap - 1);
      out[cap - 1] = '\0';
      return true;
    }
  }
  out[0] = '\0';
  return false;
}

String RouterOsClient::findReplyAttr(const CommandResult &result,
                                     const char *keyName) {
  if (!keyName || !keyName[0]) return String();
  for (uint8_t row = 0; row < result.replyCount; ++row) {
    const String value = replyAttr(result, row, keyName);
    if (value.length() > 0) return value;
  }
  return String();
}

bool RouterOsClient::writeSentence(const String *words, size_t count) {
#if RENZFI_ROUTER_API_LOG_SENTENCE_WORDS
  // Safe debug mode: logs word count and encoded (wire) length per word.
  // Never logs word content, so RouterOS credentials are never exposed.
  for (size_t i = 0; i < count; ++i) {
    Serial.printf("[router-api] write word index=%u encodedLen=%u\n",
                  static_cast<unsigned>(i),
                  static_cast<unsigned>(encodedWordSize(words[i])));
  }
#endif
  for (size_t i = 0; i < count; ++i) {
    if (!writeWord(words[i])) {
      setError("Failed writing RouterOS API word");
      return false;
    }
  }
  return endSentence();
}

void RouterOsClient::setLoginFailureReason(const char *reason) {
  if (!_logLoginIo) return;
  // First cause wins — a disconnect/cleanup path triggered by the real
  // failure must never overwrite the reason that actually caused it.
  if (_loginFailureReason.isEmpty()) _loginFailureReason = reason;
}

void RouterOsClient::logLoginReadTimeout(const char *reason, size_t partialWordLength,
                                         uint32_t sentenceDeadlineMs) {
  if (!_logLoginIo) return;
  const uint32_t now       = millis();
  const uint32_t elapsedMs = now - _loginIoStartMs;
  const int      available = _client.available();
  const bool     connected = _client.connected();
  const bool     jobIsExpired      = RouterApiTransportGate::jobExpired();
  const bool     sentenceIsExpired = static_cast<int32_t>(now - sentenceDeadlineMs) >= 0;

  // "connection_closed" means our side gave up waiting and *then* observed
  // a closed socket; "router_closed_connection" means the router hung up
  // before it ever sent a single byte back for this login attempt — i.e.
  // evidence RouterOS never replied at all, not just that we timed out.
  const char *effectiveReason = reason;
  if (strcmp(reason, "connection_closed") == 0 && _loginBytesReceived == 0) {
    effectiveReason = "router_closed_connection";
  }
  setLoginFailureReason(effectiveReason);

  Serial.println("[router-api] LOGIN RX TIMEOUT");
  Serial.println("[router-api] ---------------");
  Serial.printf("[router-api] reason=%s\n", effectiveReason);
  Serial.printf("[router-api] elapsedMs=%u\n", static_cast<unsigned>(elapsedMs));
  Serial.printf("[router-api] bytesAvailable=%d\n", available);
  Serial.printf("[router-api] socketAvailable()=%d\n", available);
  Serial.printf("[router-api] client.connected()=%d\n", connected ? 1 : 0);
  Serial.printf("[router-api] jobExpired=%d\n", jobIsExpired ? 1 : 0);
  Serial.printf("[router-api] sentenceExpired=%d\n", sentenceIsExpired ? 1 : 0);
  Serial.printf("[router-api] partialWordLength=%u\n",
                static_cast<unsigned>(partialWordLength));
  Serial.printf("[router-api] loginBytesReceived=%u\n",
                static_cast<unsigned>(_loginBytesReceived));
}

bool RouterOsClient::readByte(uint8_t &out) {
  const uint32_t deadline = millis() + _sentenceTimeoutMs;

  if (jobExpired()) {
    logLoginReadTimeout("job_deadline_exceeded", 0, deadline);
    setError("RouterOS API job timeout", "ROUTEROS_API_READ_TIMEOUT");
    return false;
  }

  while (millis() < deadline) {
    if (jobExpired()) {
      logLoginReadTimeout("job_deadline_exceeded", 0, deadline);
      setError("RouterOS API job timeout", "ROUTEROS_API_READ_TIMEOUT");
      return false;
    }
    if (!_client.connected() && _client.available() <= 0) {
      logLoginReadTimeout("connection_closed", 0, deadline);
      setError("RouterOS API connection closed while reading");
      return false;
    }
    if (_client.available() > 0) {
      const int value = _client.read();
      if (value < 0) {
        logLoginReadTimeout("socket_read_failed", 0, deadline);
        setError("RouterOS API read error");
        return false;
      }
      out = (uint8_t)value;
      ++_loginBytesReceived;
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  logLoginReadTimeout("sentence_deadline_exceeded", 0, deadline);
  setError("RouterOS API read timeout",
           _logLoginIo ? "ROUTEROS_API_READ_TIMEOUT" : "ROUTEROS_API_UNAVAILABLE");
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
    setLoginFailureReason("parser_failed");
    setError("Invalid RouterOS API length prefix");
    return false;
  }

  if (len == 0) {
    out = "";
    return true;
  }

  if (len > RenzFiConfig::ROUTER_API_MAX_WORD_LEN) {
    setLoginFailureReason("parser_failed");
    setError("RouterOS API word length exceeds safety bound");
    return false;
  }

  out = "";
  if (!out.reserve(len)) {
    setError("RouterOS API word allocation failed");
    return false;
  }

  if (_logLoginIo) {
    Serial.printf("[router-api] login reply word length=%u\n",
                  static_cast<unsigned>(len));
  }

  const uint32_t deadline = millis() + _sentenceTimeoutMs;
  while (out.length() < len) {
    if (jobExpired()) {
      logLoginReadTimeout("job_deadline_exceeded", out.length(), deadline);
      setError("RouterOS API job timeout", "ROUTEROS_API_READ_TIMEOUT");
      return false;
    }
    if (!_client.connected() && _client.available() <= 0) {
      logLoginReadTimeout("connection_closed", out.length(), deadline);
      setError("RouterOS API connection closed while reading payload");
      return false;
    }
    if (_client.available() == 0) {
      if (millis() >= deadline) {
        logLoginReadTimeout("sentence_deadline_exceeded", out.length(), deadline);
        setError("RouterOS API payload read timeout",
                 _logLoginIo ? "ROUTEROS_API_READ_TIMEOUT"
                             : "ROUTEROS_API_UNAVAILABLE");
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    const int value = _client.read();
    if (value < 0) {
      logLoginReadTimeout("socket_read_failed", out.length(), deadline);
      setError("RouterOS API read error");
      return false;
    }
    ++_loginBytesReceived;
    if (!out.concat((char)value)) {
      setLoginFailureReason("parser_failed");
      setError("RouterOS API word buffer append failed");
      return false;
    }
  }
  return true;
}

void RouterOsClient::addReplyRecord(CommandResult &out, const String *words,
                                    size_t wordCount) {
  if (out.replyCount >= _replyRecordLimit) {
    out.replyLimitReached = true;
    return;
  }

  ReplyRecord &record = out.appendReply();
  if (out.replyLimitReached && record.attrCount == 0 && out.replyCount == 0) {
    return;
  }

  for (size_t i = 0; i < wordCount; ++i) {
    if (words[i] == "!re" || words[i] == "!trap" || words[i] == "!fatal") {
      continue;
    }
    if (!record.addAttr(words[i])) {
      out.replyLimitReached = true;
      Serial.printf("[router-api] reply attr limit reached (max=%u)\n",
                    static_cast<unsigned>(ReplyRecord::MAX_ATTRS));
      break;
    }
  }
}

bool RouterOsClient::readSentence(CommandResult &out) {
  size_t wordCount = 0;

  while (true) {
    String word;
    if (!readWord(word)) return false;

    if (word.length() == 0) {
      if (wordCount == 0) continue;
      break;
    }

    if (wordCount < MAX_SENTENCE_WORDS) {
      _sentenceWords[wordCount++] = word;
    }
  }

  const char *context = "command";
  if (_logLoginIo) {
    context = "login";
  } else if (_transcriptMode == TranscriptMode::Identity) {
    context = "identity";
  }
  if (_logLoginIo) {
    // Every word RouterOS actually sent back, logged before parser
    // processing, so we can tell "router replied and parser failed" apart
    // from "router never replied" during the login regression audit.
    const uint32_t elapsedMs = millis() - _loginIoStartMs;
    Serial.println("[router-api] LOGIN RX");
    Serial.println("[router-api] ---------------");
    Serial.printf("[router-api] sentenceType=%s\n",
                  wordCount > 0 ? _sentenceWords[0].c_str() : "(empty)");
    Serial.printf("[router-api] wordCount=%u\n", static_cast<unsigned>(wordCount));
    for (size_t i = 0; i < wordCount; ++i) {
      Serial.printf("[router-api] word[%u]=%s\n", static_cast<unsigned>(i),
                    _sentenceWords[i].c_str());
    }
    Serial.printf("[router-api] elapsedMs=%u\n", static_cast<unsigned>(elapsedMs));
  }
  logParsedSentence(wordCount, context);
  applySentenceToResult(out, wordCount);
  return true;
}

bool RouterOsClient::drainCommandResult(CommandResult &out) {
  while (!out.doneReceived && !out.trapReceived && !out.fatalReceived) {
    if (!readSentence(out)) return false;
    if (_stopDrainOnReplyLimit && out.replyLimitReached) {
      disconnectInternal("reply_limit");
      out.doneReceived = true;
      return true;
    }
  }
  return true;
}

bool RouterOsClient::drainLoginResult(CommandResult &out) {
  while (!out.doneReceived && !out.trapReceived && !out.fatalReceived) {
    if (!readSentence(out)) return false;
  }
  return true;
}

void RouterOsClient::logCommandResult(const char *stage, const String &commandPath,
                                      const CommandResult &out) const {
  RENZFI_ROUTER_API_VERBOSE(
      "[router-api] inspect %s cmd=%s done=%d trap=%d fatal=%d replyCount=%u\n",
      stage ? stage : "", commandPath.c_str(), out.doneReceived ? 1 : 0,
      out.trapReceived ? 1 : 0, out.fatalReceived ? 1 : 0,
      static_cast<unsigned>(out.replyCount));
  if (out.trapReceived) {
    RENZFI_ROUTER_API_VERBOSE("[router-api] inspect %s trapMessage=%s category=%s\n",
                              stage ? stage : "", out.trapMessage.c_str(),
                              out.trapCategory.c_str());
  }
  if (out.fatalReceived) {
    RENZFI_ROUTER_API_VERBOSE("[router-api] inspect %s fatalMessage=%s category=%s\n",
                              stage ? stage : "", out.fatalMessage.c_str(),
                              out.fatalCategory.c_str());
  }
}

bool RouterOsClient::connect() {
  RouterWorkerDiagnostics::logStage("ros-connect-entry");
  RouterWorkerDiagnostics::checkStackMargin("ros-connect-entry");
  IoLock lock;
  if (_connected || _client.connected()) {
    disconnectInternal("protocol_error");
  }

  if (!ETH.connected() && !ETH.linkUp()) {
    setError("Ethernet link is down");
    return false;
  }
  if (_host.isEmpty()) {
    setError("MikroTik host is not configured");
    return false;
  }
  if (jobExpired()) {
    setError("RouterOS API job timeout", "ROUTEROS_API_READ_TIMEOUT");
    return false;
  }
  if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
    // Brief recover window (SoftAP captive churn). Still fail soft — never
    // proceed into W5500 SPI with dma_largest below one frame (Guru).
    if (!DmaMemoryMonitor::waitForDmaHeadroom(
            3000, DmaMemoryMonitor::kMinLargestDmaBlockForEthTx)) {
      DmaMemoryMonitor::logSnapshot("before tx");
      DmaMemoryMonitor::logTrace("ros-connect-eth-dma-low");
      setError("Ethernet DMA memory low — defer RouterOS connect", "ETH_DMA_LOW");
      return false;
    }
  }
  const uint32_t gateStart = millis();
  if (!RouterApiTransportGate::waitUntilConnectAllowed()) {
    setError("RouterOS API job timeout", "ROUTEROS_API_READ_TIMEOUT");
    return false;
  }
  const uint32_t gateWaitMs = millis() - gateStart;
  if (!RouterApiTransportGate::acquireSession()) {
    setError("Another RouterOS API session is active", "ROUTEROS_API_SESSION_BUSY");
    return false;
  }
  _sessionAcquired = true;

  const uint32_t jobId = RouterApiTransportGate::currentJobId();
  RENZFI_ROUTER_API_VERBOSE("[router-api] job=%u connect attempt=1\n",
                            static_cast<unsigned>(jobId));

  RouterApiTransportGate::recordConnectAttempt();

  _client.setTimeout(_connectTimeoutMs);
  const uint32_t connectStart = millis();
  RouterWorkerDiagnostics::logStage("before-network-client-connect");
  if (FinishTrace::pipelineActive()) {
    FinishTrace::BlockingOpScope::updateActiveState("TCP connect in progress",
                                                    "TCP response");
  }
  const bool connected        = _client.connect(_host.c_str(), _port);
  RouterWorkerDiagnostics::logStage("after-network-client-connect");
  const uint32_t elapsedMs    = millis() - connectStart;
  Serial.printf(
      "[activate-latency] connect_gate_wait=%u tcp_connect=%u ok=%d\n",
      (unsigned)gateWaitMs, (unsigned)elapsedMs, connected ? 1 : 0);
  RENZFI_ROUTER_API_VERBOSE("[router-api] connect returned=%d elapsedMs=%u\n",
                            connected ? 1 : 0, static_cast<unsigned>(elapsedMs));

  if (!connected) {
    DmaMemoryMonitor::logSnapshot("after tx");
    RouterApiTransportGate::recordFailure();
    setError("TCP connect to RouterOS API failed", "TCP_CONNECT_FAILED");
    disconnectInternal("protocol_error");
    return false;
  }

  _client.setTimeout(_sentenceTimeoutMs);
  _connected = true;
  _loggedIn  = false;
  _loginAttemptMode = LoginAttemptMode::NoLoginRequest;
  _lastError = "";
  RouterWorkerDiagnostics::logStage("ros-connect-exit");
  return true;
}

void RouterOsClient::disconnect(const char *closeReason) {
  IoLock lock;
  disconnectInternal(closeReason ? closeReason : "shutdown");
}

void RouterOsClient::disconnectInternal(const char *closeReason) {
  const uint32_t jobId = RouterApiTransportGate::currentJobId();
  if (_connected || _client.connected() || _sessionAcquired) {
    RENZFI_ROUTER_API_VERBOSE("[router-api] job=%u close reason=%s\n",
                              static_cast<unsigned>(jobId),
                              closeReason ? closeReason : "shutdown");
  }

  _connected = false;
  _loggedIn  = false;
  _loginAttemptMode = LoginAttemptMode::NoLoginRequest;
  _transcriptMode = TranscriptMode::None;
  _transcriptTypes = nullptr;
  _transcriptAttrs = nullptr;
  if (_client.connected()) {
    _client.flush();
    _client.stop();
  }
  if (_sessionAcquired) {
    RouterApiTransportGate::releaseSession();
    _sessionAcquired = false;
  }
  vTaskDelay(pdMS_TO_TICKS(5));
}

bool RouterOsClient::sendLoginSentence(LoginCredentialKind kind,
                                       const String &nameWord,
                                       const String &credWord) {
  // Reject name-only or credential-less login at the sentence boundary.
  // No production path may write /login with username but without password
  // (direct login) or =response= (legacy challenge login).
  if (nameWord.isEmpty()) {
    setError("RouterOS login username word is missing", "ROUTEROS_LOGIN_FAILED");
    return false;
  }
  String nameKey, nameValue;
  if (!parseAttr(nameWord, nameKey, nameValue) || nameKey != "name" ||
      nameValue.isEmpty()) {
    setError("RouterOS login username word is invalid", "ROUTEROS_LOGIN_FAILED");
    return false;
  }
  if (credWord.isEmpty()) {
    setError("RouterOS login credential word is missing", "ROUTEROS_LOGIN_FAILED");
    return false;
  }
  String credKey, credValue;
  if (!parseAttr(credWord, credKey, credValue) || credValue.isEmpty()) {
    setError("RouterOS login credential word is invalid", "ROUTEROS_LOGIN_FAILED");
    return false;
  }
  if (kind == LoginCredentialKind::Password) {
    if (credKey != "password") {
      setError("RouterOS direct login requires =password=", "ROUTEROS_LOGIN_FAILED");
      return false;
    }
  } else if (credKey != "response") {
    setError("RouterOS challenge login requires =response=", "ROUTEROS_LOGIN_FAILED");
    return false;
  }

  const String words[] = {"/login", nameWord, credWord};
  const size_t wordCount = 3;
  const size_t totalBytes = sentenceByteCount(words, wordCount);

  _loginTxCaptureLen = 0;
  _loginTxWriteCalls = 0;

  // Temporary login diagnostics (task: audit RouterOS login regression).
  // Never logs plaintext credentials — word[1]/word[2] are fingerprinted as
  // key name + value length only.
  RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] LOGIN TX WORDS");
  RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] ---------------");
  RENZFI_ROUTER_API_VERBOSE("[router-api] word[0]=%s\n", words[0].c_str());
  RENZFI_ROUTER_API_VERBOSE("[router-api] word[1]=%s (len=%u)\n", nameKey.c_str(),
                              static_cast<unsigned>(nameValue.length()));
  RENZFI_ROUTER_API_VERBOSE("[router-api] word[2]=%s (len=%u)\n", credKey.c_str(),
                              static_cast<unsigned>(credValue.length()));

  bool terminatorWritten = false;
  for (size_t i = 0; i < wordCount; ++i) {
    if (!writeWord(words[i])) {
      dumpLoginTxCapture();
      setLoginFailureReason("socket_write_failed");
      setError("Failed sending RouterOS login", "ROUTEROS_LOGIN_FAILED");
      return false;
    }
  }
  terminatorWritten = endSentence();
  if (!terminatorWritten) {
    dumpLoginTxCapture();
    setLoginFailureReason("socket_write_failed");
    setError("Failed sending RouterOS login terminator", "ROUTEROS_LOGIN_FAILED");
    return false;
  }
  dumpLoginTxCapture();
  // Self-check: expectedBytes is computed independently (word length
  // encoding rules) from what writeAndCapture() actually observed going
  // into NetworkClient::write(). Any mismatch here is itself proof of an
  // extra/missing byte in the framing, independent of RouterOS's reply.
  RENZFI_ROUTER_API_VERBOSE("[router-api] TX expectedBytes=%u actualBytes=%u match=%d\n",
                            static_cast<unsigned>(totalBytes),
                            static_cast<unsigned>(_loginTxCaptureLen),
                            totalBytes == _loginTxCaptureLen ? 1 : 0);

#if RENZFI_ROUTER_API_LOG_SENTENCE_WORDS
  logLoginWriteDiagnostics(kind, nameValue.length(), credValue.length(), wordCount,
                           totalBytes, terminatorWritten);
#endif

  // Record what was actually sent only after the full sentence, including
  // the zero-length terminator, is on the wire.
  _loginAttemptMode =
      (kind == LoginCredentialKind::Password)
          ? LoginAttemptMode::DirectPasswordLoginSent
          : LoginAttemptMode::ChallengeResponseSent;

  // Never log credential content — lengths only.
  RENZFI_ROUTER_API_VERBOSE(
      "[router-api] login payload usernameLen=%u credentialLen=%u words=%u "
      "bytes=%u\n",
      static_cast<unsigned>(nameValue.length()),
      static_cast<unsigned>(credValue.length()), static_cast<unsigned>(wordCount),
      static_cast<unsigned>(totalBytes));
  RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] login write complete");
  return true;
}

void RouterOsClient::logLoginWriteDiagnostics(LoginCredentialKind kind,
                                              size_t usernameLen,
                                              size_t credentialLen,
                                              size_t wordCount,
                                              size_t byteCount,
                                              bool terminatorWritten) {
  const char *mode =
      (kind == LoginCredentialKind::Password) ? "direct-password" : "challenge-response";
  Serial.printf(
      "[router-api] login write mode=%s usernameLen=%u passwordLen=%u words=%u "
      "bytes=%u terminator=%d\n",
      mode, static_cast<unsigned>(usernameLen),
      static_cast<unsigned>(credentialLen), static_cast<unsigned>(wordCount),
      static_cast<unsigned>(byteCount), terminatorWritten ? 1 : 0);
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
    const ReplyRecord &record = result.replyAt(i);
    for (uint8_t j = 0; j < record.attrCount; ++j) {
      String key;
      String value;
      if (parseAttr(record.attr(j), key, value) && key == "ret" &&
          !value.isEmpty()) {
        return value;
      }
    }
  }
  return "";
}

bool RouterOsClient::evaluateLoginResult(const CommandResult &result,
                                         bool &needChallenge,
                                         String &challengeOut) {
  needChallenge = false;
  challengeOut.clear();

  if (result.fatalReceived) {
    setLoginFailureReason("auth_fatal");
    setError(result.fatalMessage.isEmpty() ? "RouterOS fatal login error"
                                           : result.fatalMessage,
             "ROUTEROS_API_AUTH_FATAL");
    return false;
  }
  if (result.trapReceived) {
    setLoginFailureReason("auth_trap");
    setError(result.trapMessage.isEmpty() ? "Invalid RouterOS API username or password"
                                          : result.trapMessage,
             "ROUTEROS_API_AUTH_TRAP");
    return false;
  }

  // A legacy (pre-6.43) RouterOS ignores the name/password attributes on
  // the first /login sentence and always answers with a challenge; detect
  // that here so modern direct-login and legacy challenge-response both
  // work through the same call path. Do NOT treat this as authenticated.
  challengeOut = challengeFromResult(result);
  if (!challengeOut.isEmpty()) {
    needChallenge = true;
    return true;
  }

  if (result.doneReceived) {
    if (_loginAttemptMode == LoginAttemptMode::NoLoginRequest) {
      setError("RouterOS login success without outbound login request",
               "ROUTEROS_API_PROTOCOL_ERROR");
      return false;
    }
    return true;
  }

  // A sentence was fully parsed (!re/!done/!trap/!fatal would have already
  // returned above) but none of the recognized outcomes matched — RouterOS
  // replied, the byte stream was well-formed, yet the reply doesn't resolve
  // to a login outcome we understand. That is a parser/protocol mismatch,
  // not a transport timeout.
  setLoginFailureReason("parser_failed");
  setError("RouterOS login incomplete", "ROUTEROS_LOGIN_FAILED");
  return false;
}

bool RouterOsClient::finalizeLoginSuccess(const CommandResult &result) {
  if (_loginAttemptMode == LoginAttemptMode::NoLoginRequest) {
    setLoginFailureReason("protocol_error");
    setError("RouterOS login success without outbound login request",
             "ROUTEROS_API_PROTOCOL_ERROR");
    return false;
  }
  if (result.trapReceived || result.fatalReceived || !result.doneReceived) {
    setLoginFailureReason("protocol_error");
    setError("RouterOS login terminal success preconditions not met",
             "ROUTEROS_API_PROTOCOL_ERROR");
    return false;
  }
  if (_loginAttemptMode != LoginAttemptMode::DirectPasswordLoginSent &&
      _loginAttemptMode != LoginAttemptMode::ChallengeResponseSent) {
    setLoginFailureReason("protocol_error");
    setError("RouterOS login success with invalid attempt mode",
             "ROUTEROS_API_PROTOCOL_ERROR");
    return false;
  }

  flushReceiveBuffer();
  _loggedIn = true;
  RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] session login ok");
  return true;
}

bool RouterOsClient::loginWithPassword() {
  resetCommandResult(_ioScratch);

  // Always send name AND password together in the first /login sentence.
  // Sending name-only (no password word) previously let RouterOS return
  // !done for an incomplete/unauthenticated login, which is how a wrong
  // password was falsely accepted. The password is transmitted here in
  // full — never omitted, truncated, hashed, or masked.
  const uint32_t loginStart = _loginIoStartMs ? _loginIoStartMs : millis();
  if (!sendLoginSentence(LoginCredentialKind::Password, "=name=" + _username,
                         "=password=" + _password)) {
    return false;
  }
  Serial.printf("[activate-latency] login_tx=%u\n",
                (unsigned)(millis() - loginStart));
  RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] before login read");
  if (!drainLoginResult(_ioScratch)) return false;
  Serial.printf("[activate-latency] login_rx=%u\n",
                (unsigned)(millis() - loginStart));

  bool needChallenge = false;
  String challenge;
  if (!evaluateLoginResult(_ioScratch, needChallenge, challenge)) {
    return false;
  }
  Serial.printf("[activate-latency] login_challenge=%d\n",
                needChallenge ? 1 : 0);
  if (needChallenge) return loginWithChallenge(challenge);
  return finalizeLoginSuccess(_ioScratch);
}

bool RouterOsClient::loginWithChallenge(const String &challengeHex) {
  const String response = md5RouterOsResponse(_password, challengeHex);
  if (response.isEmpty()) {
    setLoginFailureReason("challenge_response_build_failed");
    setError("Failed building RouterOS challenge response", "ROUTEROS_LOGIN_FAILED");
    return false;
  }

  resetCommandResult(_ioScratch);
  const uint32_t challengeStart = millis();
  if (!sendLoginSentence(LoginCredentialKind::ChallengeResponse,
                         "=name=" + _username, "=response=" + response)) {
    return false;
  }
  RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] before login read");
  if (!drainLoginResult(_ioScratch)) return false;
  Serial.printf("[activate-latency] login_challenge_rx=%u\n",
                (unsigned)(millis() - challengeStart));

  bool needChallenge = false;
  String ignored;
  if (!evaluateLoginResult(_ioScratch, needChallenge, ignored)) return false;
  if (needChallenge) {
    setLoginFailureReason("challenge_loop");
    setError("RouterOS login challenge loop", "ROUTEROS_LOGIN_FAILED");
    return false;
  }
  return finalizeLoginSuccess(_ioScratch);
}

bool RouterOsClient::login() {
  RouterWorkerDiagnostics::logStage("ros-login-entry");
  RouterWorkerDiagnostics::checkStackMargin("ros-login-entry");
  IoLock lock;
  if (!_connected) {
    setError("Not connected to RouterOS API", "ROUTEROS_LOGIN_FAILED");
    return false;
  }
  // Reject empty credentials before any login I/O. An empty password must
  // never be treated as "connectivity implies authentication".
  if (_username.isEmpty()) {
    setError("RouterOS API username is not configured", "ROUTEROS_LOGIN_FAILED");
    return false;
  }
  if (_password.isEmpty()) {
    setError("RouterOS API password is not configured", "ROUTEROS_LOGIN_FAILED");
    return false;
  }

  _loggedIn = false;
  _loginAttemptMode = LoginAttemptMode::NoLoginRequest;

  _logLoginIo         = (RENZFI_VERBOSE_ROUTER_API != 0);
  _loginIoStartMs      = millis();
  _loginBytesReceived  = 0;
  _loginFailureReason  = "";

  // Task 1: prove the exact timeout policy in effect for this attempt
  // before doing any I/O — connect/io timeouts as configured on this
  // client instance, plus the worker's overall job deadline (a separate,
  // independently-enforced gate — see RouterApiTransportGate::jobExpired).
  const uint32_t nowMs           = millis();
  const uint32_t sentenceDeadline = nowMs + _sentenceTimeoutMs;
  const uint32_t jobDeadline      = RouterApiTransportGate::currentJobDeadlineMs();
  RENZFI_ROUTER_API_VERBOSE("[router-api] connect timeout=%u\n",
                            static_cast<unsigned>(_connectTimeoutMs));
  RENZFI_ROUTER_API_VERBOSE("[router-api] io timeout=%u\n",
                            static_cast<unsigned>(_sentenceTimeoutMs));
  RENZFI_ROUTER_API_VERBOSE("[router-api] worker timeout=%u\n",
                            static_cast<unsigned>(jobDeadline > nowMs ? jobDeadline - nowMs : 0));
  RENZFI_ROUTER_API_VERBOSE(
      "[router-api] job=%u login start millis=%u sentenceDeadline=%u "
      "jobDeadline=%u\n",
      static_cast<unsigned>(RouterApiTransportGate::currentJobId()),
      static_cast<unsigned>(nowMs), static_cast<unsigned>(sentenceDeadline),
      static_cast<unsigned>(jobDeadline));

  RouterWorkerDiagnostics::logStage("ros-login-before-loginWithPassword");
  if (FinishTrace::pipelineActive()) {
    FinishTrace::BlockingOpScope::updateActiveState("RouterOS login exchange",
                                                    "RouterOS reply");
  }
  const bool ok = loginWithPassword();
  RouterWorkerDiagnostics::logStage("ros-login-after-loginWithPassword");
  const uint32_t elapsedMs = millis() - _loginIoStartMs;
  Serial.printf("[activate-latency] ros_login=%u ok=%d\n",
                (unsigned)elapsedMs, ok ? 1 : 0);

  if (ok) {
    RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] LOGIN SUCCESS");
    RENZFI_ROUTER_API_VERBOSE("[router-api] elapsedMs=%u\n",
                              static_cast<unsigned>(elapsedMs));
  } else {
    const char *failureReason =
        _loginFailureReason.isEmpty() ? "unknown" : _loginFailureReason.c_str();
    RENZFI_ROUTER_API_VERBOSE_LINE("[router-api] LOGIN FAILED");
    RENZFI_ROUTER_API_VERBOSE("[router-api] reason=%s\n", failureReason);
    RENZFI_ROUTER_API_VERBOSE("[router-api] elapsedMs=%u\n",
                              static_cast<unsigned>(elapsedMs));
    RENZFI_ROUTER_API_VERBOSE("[router-api] lastErrorCode=%s\n",
                              _lastErrorCode.c_str());
    Serial.printf("[router-api] LOGIN FAILED host=%s user=%s source=%s reason=%s code=%s\n",
                  _host.c_str(), _username.c_str(),
                  _credentialSource.isEmpty() ? "unspecified" : _credentialSource.c_str(),
                  failureReason, _lastErrorCode.c_str());
  }
  _logLoginIo = false;

  if (!ok) {
    RouterApiTransportGate::recordFailure();
    const char *reason =
        (_lastErrorCode == "ROUTEROS_API_AUTH_FATAL" ||
         _lastErrorCode == "ROUTEROS_API_PROTOCOL_ERROR")
            ? "protocol_error"
            : "login_failed";
    disconnectInternal(reason);
  }
  RouterWorkerDiagnostics::logStage("ros-login-exit");
  return ok;
}

bool RouterOsClient::executeCommand(const String &commandPath,
                                    const String &attribute,
                                    CommandResult &out) {
  if (attribute.isEmpty()) return executeCommand(commandPath, out);
  const String attrs[] = {attribute};
  return executeCommand(commandPath, attrs, 1, out);
}

bool RouterOsClient::executeCommand(const String &commandPath,
                                    const String *attributes,
                                    size_t attributeCount, CommandResult &out) {
  return executeCommand(commandPath, attributes, attributeCount, &out);
}

bool RouterOsClient::drainTrailingDone() {
  for (uint8_t guard = 0; guard < 8 && !_ioScratch.doneReceived && !_ioScratch.fatalReceived;
       ++guard) {
    initializeCommandResult(_ioScratch);
    if (!readSentence(_ioScratch)) return false;
  }
  return _ioScratch.doneReceived;
}

bool RouterOsClient::isMissingPackageTrapMessage(const String &message) {
  String lower = message;
  lower.toLowerCase();
  return lower.indexOf("no such command") >= 0 ||
         lower.indexOf("bad command name") >= 0 ||
         lower.indexOf("input does not match") >= 0;
}

bool RouterOsClient::executeCommandImpl(const String &commandPath,
                                        const String *attributes,
                                        size_t attributeCount, CommandResult *out,
                                        CommandMode mode, bool *missingPackageOut) {
  if (missingPackageOut) *missingPackageOut = false;
  if (!out) {
    setError("Command result output is null", "ROUTEROS_API_PROTOCOL_ERROR");
    return false;
  }
  if (attributes == nullptr && attributeCount > 0) {
    setError("Command attributes pointer is null", "ROUTEROS_API_PROTOCOL_ERROR");
    return false;
  }

  const uint32_t cmdStartMs = millis();
  Serial.println("[router-api] START");
  Serial.println(commandPath.c_str());

  RouterWorkerDiagnostics::logStage("exec-cmd-entry");
  RouterWorkerDiagnostics::checkStackMargin("exec-cmd-entry");
  IoLock lock;
  if (!_connected) {
    setError("Not connected to RouterOS API", "ROUTEROS_API_UNAVAILABLE");
    Serial.printf("[router-api] END elapsed=%u ms (not connected)\n",
                  static_cast<unsigned>(millis() - cmdStartMs));
    return false;
  }
  if (!_loggedIn) {
    setError("RouterOS API not authenticated", "API_LOGIN_FAILED");
    Serial.printf("[router-api] END elapsed=%u ms (not logged in)\n",
                  static_cast<unsigned>(millis() - cmdStartMs));
    return false;
  }
  if (attributeCount + 1 > MAX_SENTENCE_WORDS) {
    setError("RouterOS command has too many attributes", "API_COMMAND_FAILED");
    Serial.printf("[router-api] END elapsed=%u ms (too many attrs)\n",
                  static_cast<unsigned>(millis() - cmdStartMs));
    return false;
  }

  initializeCommandResult(*out);
  _sentenceWords[0] = commandPath;
  for (size_t i = 0; i < attributeCount; ++i) {
    _sentenceWords[i + 1] = attributes[i];
  }

  // MikroTik CPU protection: never issue back-to-back RouterOS commands
  // faster than the configured minimum spacing (wider automatically when
  // the last observed cpu-load sample indicates the router is under
  // pressure). This is the single choke point every command path in this
  // firmware funnels through.
  RouterApiTransportGate::waitBeforeCommand();
  if (jobExpired()) {
    setError("RouterOS API job timeout", "ROUTEROS_API_READ_TIMEOUT");
    Serial.printf("[router-api] END elapsed=%u ms (job expired before write)\n",
                  static_cast<unsigned>(millis() - cmdStartMs));
    return false;
  }

  if (!DmaMemoryMonitor::hasEthTransmitHeadroom()) {
    if (!DmaMemoryMonitor::waitForDmaHeadroom(
            3000, DmaMemoryMonitor::kMinLargestDmaBlockForEthTx)) {
      DmaMemoryMonitor::logSnapshot("before router-write");
      setError("Ethernet SPI DMA memory low — defer RouterOS command",
               "SPI_DMA_ALLOCATION_FAILED");
      Serial.printf("[router-api] END elapsed=%u ms (SPI DMA low)\n",
                    static_cast<unsigned>(millis() - cmdStartMs));
      return false;
    }
  }

  RouterWorkerDiagnostics::logStage("exec-cmd-before-writeSentence");
  if (!writeSentence(_sentenceWords, 1 + attributeCount)) {
    DmaMemoryMonitor::logSnapshot("after tx");
    setError("Failed sending RouterOS command", "API_COMMAND_FAILED");
    disconnectInternal("protocol_error");
    Serial.printf("[router-api] END elapsed=%u ms (write failed)\n",
                  static_cast<unsigned>(millis() - cmdStartMs));
    return false;
  }

  RouterWorkerDiagnostics::logStage("exec-cmd-before-drainCommandResult");
  if (FinishTrace::pipelineActive()) {
    FinishTrace::BlockingOpScope::updateActiveState("awaiting command reply",
                                                    "RouterOS reply");
  }
  if (!drainCommandResult(*out)) {
    DmaMemoryMonitor::logSnapshot("after router-read");
    RouterApiTransportGate::recordCommandCompleted();
    RouterApiTransportGate::recordFailure();
    disconnectInternal(jobExpired() ? "timeout" : "protocol_error");
    Serial.printf("[router-api] END elapsed=%u ms (read failed)\n",
                  static_cast<unsigned>(millis() - cmdStartMs));
    return false;
  }
  RouterApiTransportGate::recordCommandCompleted();
  RouterWorkerDiagnostics::logStage("exec-cmd-after-drainCommandResult");
  logCommandResult("command", commandPath, *out);

  if (out->fatalReceived) {
    RouterApiTransportGate::recordFailure();
    setError(out->fatalMessage.isEmpty() ? "RouterOS fatal error" : out->fatalMessage,
             "ROUTEROS_API_FATAL");
    disconnectInternal("protocol_error");
    Serial.printf("[router-api] END elapsed=%u ms (fatal)\n",
                  static_cast<unsigned>(millis() - cmdStartMs));
    return false;
  }
  if (out->trapReceived) {
    const String trapMsg =
        out->trapMessage.isEmpty() ? _lastError : out->trapMessage;
    Serial.printf("[router-api] TRAP cmd=%s category=%s message=%s\n",
                  commandPath.c_str(), out->trapCategory.c_str(), trapMsg.c_str());
    if (mode == CommandMode::OptionalProbe &&
        isMissingPackageTrapMessage(trapMsg)) {
      if (missingPackageOut) *missingPackageOut = true;
      (void)drainTrailingDone();
      Serial.printf("[router-api] END elapsed=%u ms (optional missing package)\n",
                    static_cast<unsigned>(millis() - cmdStartMs));
      return false;
    }
    RouterApiTransportGate::recordFailure();
    setError(out->trapMessage.isEmpty() ? "RouterOS API trap" : out->trapMessage,
             "API_TRAP");
    disconnectInternal("protocol_error");
    Serial.printf("[router-api] END elapsed=%u ms (trap) cmd=%s message=%s\n",
                  static_cast<unsigned>(millis() - cmdStartMs), commandPath.c_str(),
                  trapMsg.c_str());
    return false;
  }

  Serial.printf("[router-api] END elapsed=%u ms\n",
                static_cast<unsigned>(millis() - cmdStartMs));
  return true;
}

bool RouterOsClient::executeCommand(const String &commandPath,
                                    const String *attributes,
                                    size_t attributeCount, CommandResult *out) {
  return executeCommandImpl(commandPath, attributes, attributeCount, out,
                            CommandMode::Normal, nullptr);
}

bool RouterOsClient::executeOptionalCommand(const String &commandPath,
                                            CommandResult &out,
                                            bool &missingPackageOut) {
  missingPackageOut = false;
  const uint32_t priorTimeout = _sentenceTimeoutMs;
  _sentenceTimeoutMs =
      priorTimeout > RenzFiConfig::ROUTER_WIFI_OPTIONAL_CMD_MS
          ? RenzFiConfig::ROUTER_WIFI_OPTIONAL_CMD_MS
          : priorTimeout;

  Serial.println("[router-api] START optional");
  Serial.println(commandPath.c_str());
  const uint32_t probeStartMs = millis();

  const bool ok = executeCommandImpl(commandPath, nullptr, 0, &out,
                                     CommandMode::OptionalProbe,
                                     &missingPackageOut);

  _sentenceTimeoutMs = priorTimeout;

  Serial.printf("[router-api] END optional elapsed=%u ms ok=%d missing=%d\n",
                static_cast<unsigned>(millis() - probeStartMs), ok ? 1 : 0,
                missingPackageOut ? 1 : 0);
  return ok;
}

bool RouterOsClient::executeCommand(const String &commandPath, CommandResult &out) {
  return executeCommand(commandPath, static_cast<const String *>(nullptr), 0, &out);
}

#if RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC
bool RouterOsClient::runProtocolDiagnostic(ProtocolTranscript &out) {
  out = ProtocolTranscript{};
  _transcriptMode  = TranscriptMode::Login;
  _transcriptTypes = &out.loginSentenceTypes;
  _transcriptAttrs = nullptr;

  {
    IoLock lock;
    disconnect();
    if (!connect()) {
      out.stage        = "connect";
      out.errorCode    = _lastErrorCode;
      out.errorMessage = _lastError;
      _transcriptMode  = TranscriptMode::None;
      _transcriptTypes = nullptr;
      return false;
    }

    _logLoginIo     = true;
    _loginIoStartMs = millis();
    out.loginOk     = loginWithPassword();
    _logLoginIo     = false;

    if (!out.loginOk) {
      out.stage        = "login";
      out.errorCode    = _lastErrorCode;
      out.errorMessage = _lastError;
      disconnect();
      _transcriptMode  = TranscriptMode::None;
      _transcriptTypes = nullptr;
      return false;
    }

    _transcriptMode  = TranscriptMode::Identity;
    _transcriptTypes = &out.identitySentenceTypes;
    _transcriptAttrs = &out.identityAttributes;

    const bool cmdOk = executeCommand("/system/identity/print", _ioScratch);
    out.identityOk = cmdOk && _ioScratch.doneReceived && !_ioScratch.trapReceived &&
                     !_ioScratch.fatalReceived;

    if (!out.identityOk) {
      out.stage        = "inspect";
      out.errorCode    = _lastErrorCode;
      out.errorMessage = _lastError;
      if (out.errorMessage.isEmpty() && _ioScratch.fatalReceived) {
        out.errorCode    = "ROUTEROS_API_FATAL";
        out.errorMessage = _ioScratch.fatalMessage;
      } else if (out.errorMessage.isEmpty() && _ioScratch.trapReceived) {
        out.errorCode    = "API_TRAP";
        out.errorMessage = _ioScratch.trapMessage;
      }
    }

    disconnect();
  }

  _transcriptMode  = TranscriptMode::None;
  _transcriptTypes = nullptr;
  _transcriptAttrs = nullptr;
  return out.identityOk;
}
#endif
