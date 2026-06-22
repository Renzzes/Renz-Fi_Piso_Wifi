#pragma once

#include <Arduino.h>
#include <NetworkClient.h>

// RouterOS binary API client over W5500 Ethernet (ETH.h).
// Uses NetworkClient — the ESP32 3.x socket type bound to the wired interface.
class RouterOsClient {
 public:
  static constexpr size_t MAX_SENTENCE_WORDS = 32;
  static constexpr size_t MAX_REPLY_RECORDS  = 16;

  struct ReplyRecord {
    String attrs[12];
    uint8_t attrCount = 0;
  };

  struct CommandResult {
    bool doneReceived  = false;
    bool trapReceived  = false;
    bool fatalReceived = false;
    String trapMessage;
    String fatalMessage;
    ReplyRecord replies[MAX_REPLY_RECORDS];
    uint8_t replyCount = 0;
  };

  static bool parseAttr(const String &word, String &key, String &value);

  void setTimeouts(uint32_t connectMs, uint32_t ioMs);
  void setCredentials(const String &host, const String &username,
                      const String &password, uint16_t port = 8728);

  bool connect();
  bool login();
  void disconnect();

  // Sends one API sentence (command path + optional attribute words), reads until
  // !done, !trap, or !fatal. Returns false on transport/timeout errors.
  bool executeCommand(const String &commandPath, CommandResult &out);
  bool executeCommand(const String &commandPath, const String &attribute,
                      CommandResult &out);
  bool executeCommand(const String &commandPath, const String *attributes,
                      size_t attributeCount, CommandResult &out);

  bool isConnected() const { return _connected; }
  const String &lastError() const { return _lastError; }

 private:
  NetworkClient _client;
  String _host;
  String _username;
  String _password;
  uint16_t _port = 8728;
  uint32_t _connectTimeoutMs = 5000;
  uint32_t _ioTimeoutMs      = 8000;
  bool _connected            = false;
  bool _loggedIn             = false;
  String _lastError;

  void setError(const String &message);
  bool writeWord(const String &word);
  bool endSentence();
  bool writeSentence(const String *words, size_t count);
  bool readByte(uint8_t &out);
  bool readWord(String &out);
  bool readSentence(CommandResult &out);
  bool drainCommandResult(CommandResult &out);
  void addReplyRecord(CommandResult &out, const String *words, size_t wordCount);
  bool sendLoginSentence(const String &nameWord, const String &credWord);
  bool loginWithPassword();
  bool loginWithChallenge(const String &challengeHex);
  static String challengeFromResult(const CommandResult &result);
  static String md5RouterOsResponse(const String &password,
                                    const String &challengeHex);
  static bool hexToBytes(const String &hex, uint8_t *out, size_t outMax,
                         size_t &written);
  static String bytesToHex(const uint8_t *data, size_t len);
};
