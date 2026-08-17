#pragma once

#include <Arduino.h>
#include <NetworkClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// RouterOS binary API client over W5500 Ethernet (ETH.h).
class RouterOsClient {
 public:
  static constexpr size_t MAX_SENTENCE_WORDS = 32;
  static constexpr size_t MAX_REPLY_RECORDS  = 32;

  // RouterOS wireless replies often carry 14–18 attributes. Inline storage
  // covers typical rows; overflow is allocated only when needed.
  struct ReplyRecord {
    static constexpr uint8_t INLINE_ATTRS = 8;
    static constexpr uint8_t MAX_ATTRS    = 24;

    String inlineAttrs[INLINE_ATTRS];
    String *overflowAttrs     = nullptr;
    uint8_t overflowCapacity  = 0;
    uint8_t attrCount         = 0;

    ~ReplyRecord() { freeOverflow(); }
    ReplyRecord() = default;
    ReplyRecord(const ReplyRecord &) = delete;
    ReplyRecord &operator=(const ReplyRecord &) = delete;
    ReplyRecord(ReplyRecord &&other) noexcept;
    ReplyRecord &operator=(ReplyRecord &&other) noexcept;

    void clearKeepCapacity();
    void freeOverflow();
    bool addAttr(String word);
    const String &attr(uint8_t index) const;
  };

  struct CommandResult {
    static constexpr uint8_t INLINE_REPLIES = 4;

    bool doneReceived       = false;
    bool trapReceived       = false;
    bool fatalReceived      = false;
    bool replyLimitReached  = false;
    String trapMessage;
    String trapCategory;
    String fatalMessage;
    String fatalCategory;
    ReplyRecord inlineReplies[INLINE_REPLIES];
    ReplyRecord *overflowReplies    = nullptr;
    uint8_t overflowReplyCapacity   = 0;
    uint8_t replyCount              = 0;

    ~CommandResult() { freeOverflow(); }
    CommandResult() = default;
    CommandResult(const CommandResult &) = delete;
    CommandResult &operator=(const CommandResult &) = delete;
    CommandResult(CommandResult &&other) noexcept;
    CommandResult &operator=(CommandResult &&other) noexcept;

    void clearKeepCapacity();
    void freeOverflow();
    ReplyRecord &replyAt(uint8_t index);
    const ReplyRecord &replyAt(uint8_t index) const;
    ReplyRecord &appendReply();
  };

  // Sanitized read-only protocol diagnostic transcript (no secrets).
  struct ProtocolTranscript {
    bool   loginOk = false;
    bool   identityOk = false;
    String stage;
    String errorCode;
    String errorMessage;
    String loginSentenceTypes;
    String identitySentenceTypes;
    String identityAttributes;
  };

  RouterOsClient();
  ~RouterOsClient();

  static bool parseAttr(const String &word, String &key, String &value);

  void setTimeouts(uint32_t connectMs, uint32_t ioMs);
  void setCredentials(const String &host, const String &username,
                      const String &password, uint16_t port = 8728);
  void setCommandReplyLimits(uint8_t maxRecords, bool stopDrainOnLimit);
  void resetCommandReplyLimits();

  static void initializeCommandResult(CommandResult &out);

  bool connect();
  bool login();
  void disconnect(const char *closeReason = "shutdown");

  bool executeCommand(const String &commandPath, CommandResult &out);
  bool executeCommand(const String &commandPath, const String &attribute,
                      CommandResult &out);
  bool executeCommand(const String &commandPath, const String *attributes,
                      size_t attributeCount, CommandResult &out);
  bool executeCommand(const String &commandPath, const String *attributes,
                      size_t attributeCount, CommandResult *out);

  /** Probe optional RouterOS paths without tearing down the session when the
   *  command is unavailable (e.g. legacy wireless vs wifiwave2). */
  bool executeOptionalCommand(const String &commandPath, CommandResult &out,
                              bool &missingPackageOut);

  void setCredentialSource(const char *source);

  // Connect, login, run /system/identity/print once, disconnect. Read-only.
  // Compiled only when RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC=1.
#if RENZFI_ROUTER_API_PROTOCOL_DIAGNOSTIC
  bool runProtocolDiagnostic(ProtocolTranscript &out);
#endif

  bool isConnected() const { return _connected; }
  bool isLoggedIn() const { return _loggedIn; }
  bool socketConnected() { return _client.connected(); }
  const String &lastError() const { return _lastError; }
  const String &lastErrorCode() const { return _lastErrorCode; }

 private:
  enum class TranscriptMode : uint8_t { None = 0, Login, Identity };

  // Records what login sentence was actually written on the wire.
  // _loggedIn may only be set after DirectPasswordLoginSent or
  // ChallengeResponseSent and a terminal !done with no !trap/!fatal.
  enum class LoginAttemptMode : uint8_t {
    NoLoginRequest = 0,
    DirectPasswordLoginSent,
    ChallengeResponseSent,
  };

  enum class LoginCredentialKind : uint8_t { Password = 0, ChallengeResponse };

  class IoLock {
   public:
    IoLock();
    ~IoLock();
    IoLock(const IoLock &) = delete;
    IoLock &operator=(const IoLock &) = delete;

   private:
    bool _held = false;
  };

  NetworkClient _client;
  String _host;
  String _username;
  String _password;
  uint16_t _port = 8728;
  uint32_t _connectTimeoutMs = 5000;
  uint32_t _sentenceTimeoutMs = 2000;
  uint8_t _replyRecordLimit = MAX_REPLY_RECORDS;
  bool _stopDrainOnReplyLimit = false;
  bool _connected            = false;
  bool _loggedIn             = false;
  bool _logLoginIo           = false;
  bool _sessionAcquired      = false;
  // Set the instant login() begins writing; used only to compute elapsedMs
  // for the temporary login RX/timeout diagnostics (never exposes secrets).
  uint32_t _loginIoStartMs   = 0;
  // Total bytes actually received during the current login attempt. Lets
  // the diagnostics distinguish "RouterOS never replied" (0 bytes) from
  // "RouterOS replied but the parser/connection failed partway through".
  uint32_t _loginBytesReceived = 0;
  // First specific failure reason recorded during the current login
  // attempt — one of: sentence_deadline_exceeded, job_deadline_exceeded,
  // connection_closed, router_closed_connection, socket_read_failed,
  // parser_failed. Never a generic "timeout".
  String _loginFailureReason;
  String _credentialSource;
  // Raw-byte capture of the exact bytes handed to NetworkClient::write()
  // while building the /login sentence — evidence-only, read-only mirror
  // of what already went on the wire. Never influences what is sent.
  static constexpr size_t LOGIN_TX_CAPTURE_MAX = 256;
  uint8_t _loginTxCapture[LOGIN_TX_CAPTURE_MAX];
  size_t _loginTxCaptureLen   = 0;
  size_t _loginTxWriteCalls   = 0;
  LoginAttemptMode _loginAttemptMode = LoginAttemptMode::NoLoginRequest;
  String _lastError;
  String _lastErrorCode;

  TranscriptMode _transcriptMode = TranscriptMode::None;
  String *_transcriptTypes = nullptr;
  String *_transcriptAttrs = nullptr;

  String _sentenceWords[MAX_SENTENCE_WORDS];
  CommandResult _ioScratch;

  void resetCommandResult(CommandResult &out);
  void setError(const String &message, const char *code = "ROUTEROS_API_UNAVAILABLE");
  void disconnectInternal(const char *closeReason);
  bool jobExpired() const;
  void flushReceiveBuffer();
  bool drainLoginResult(CommandResult &out);
  bool loginWithPassword();
  bool loginWithChallenge(const String &challengeHex);
  bool evaluateLoginResult(const CommandResult &result, bool &needChallenge,
                           String &challengeOut);
  bool finalizeLoginSuccess(const CommandResult &result);
  void logLoginWriteDiagnostics(LoginCredentialKind kind, size_t usernameLen,
                                size_t credentialLen, size_t wordCount,
                                size_t byteCount, bool terminatorWritten);
  void logParsedSentence(size_t wordCount, const char *context);
  // Temporary login regression diagnostics — no-op unless _logLoginIo is
  // set (i.e. only fires for RouterOsClient::login(), never for ordinary
  // command traffic). Never logs credential content.
  void logLoginReadTimeout(const char *reason, size_t partialWordLength,
                           uint32_t sentenceDeadlineMs);
  // Records the first specific failure reason for the current login
  // attempt (subsequent calls are ignored so the *root* cause survives
  // any cleanup-path noise). No-op unless _logLoginIo is set.
  void setLoginFailureReason(const char *reason);
  // Pass-through wrapper around NetworkClient::write(): sends exactly the
  // same bytes, returns exactly the same value, changes nothing about what
  // goes on the wire. When _logLoginIo is set, it additionally mirrors the
  // bytes into _loginTxCapture and logs this call's requested/returned
  // byte counts, so the login sentence's *actual* wire bytes are directly
  // observable instead of inferred from logical word logging.
  size_t writeAndCapture(const uint8_t *data, size_t len);
  void dumpLoginTxCapture() const;
  static uint32_t crc32Of(const uint8_t *data, size_t len);
  void logCommandResult(const char *stage, const String &commandPath,
                        const CommandResult &out) const;
  void applySentenceToResult(CommandResult &out, size_t wordCount);
  void appendTranscriptAttr(const String &key, const String &value);

  bool writeWord(const String &word);
  bool endSentence();
  bool writeSentence(const String *words, size_t count);
  static size_t encodedWordSize(const String &word);
  static size_t sentenceByteCount(const String *words, size_t count);
  bool readByte(uint8_t &out);
  bool readWord(String &out);
  bool readSentence(CommandResult &out);
  bool drainCommandResult(CommandResult &out);
  bool drainTrailingDone();
  void addReplyRecord(CommandResult &out, const String *words, size_t wordCount);
  bool sendLoginSentence(LoginCredentialKind kind, const String &nameWord,
                         const String &credWord);

  enum class CommandMode : uint8_t { Normal, OptionalProbe };
  static bool isMissingPackageTrapMessage(const String &message);
  bool executeCommandImpl(const String &commandPath, const String *attributes,
                          size_t attributeCount, CommandResult *out,
                          CommandMode mode, bool *missingPackageOut);

  static String challengeFromResult(const CommandResult &result);
  static String md5RouterOsResponse(const String &password,
                                    const String &challengeHex);
  static bool hexToBytes(const String &hex, uint8_t *out, size_t outMax,
                         size_t &written);
  static String bytesToHex(const uint8_t *data, size_t len);
};
