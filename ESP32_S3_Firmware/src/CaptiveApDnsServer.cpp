#include "CaptiveApDnsServer.h"

#include <AsyncUDP.h>
#include <lwip/def.h>

#include "ManagementApConfig.h"
#include "SetupDnsPolicy.h"

namespace {

constexpr uint16_t kDnsPort = 53;
constexpr uint16_t kDnsHeaderSize = 12;
constexpr uint16_t kDnsMinReqLen = 17;
constexpr uint16_t kDnsOffsetDomainName = kDnsHeaderSize;
constexpr uint32_t kDefaultTtl = 60;

enum : uint16_t {
  kDnsTypeA = 1,
  kDnsTypeSoa = 6,
  kDnsClassIn = 1,
};

struct DnsHeader {
  uint16_t id;
  uint16_t flags;
  uint16_t qdCount;
  uint16_t anCount;
  uint16_t nsCount;
  uint16_t arCount;
};

struct DnsQuestion {
  const uint8_t *qName;
  uint16_t qNameLength;
  uint16_t qType;
  uint16_t qClass;
};

bool requestIncludesOnlyOneQuestion(DnsHeader &header) {
  header.arCount = 0;
  return ntohs(header.qdCount) == 1 && header.anCount == 0 &&
         header.nsCount == 0;
}

bool isQuery(const DnsHeader &header) {
  return (ntohs(header.flags) & 0x8000U) == 0;
}

void sendARecordReply(AsyncUDP &udp, AsyncUDPPacket &req, DnsHeader &header,
                      DnsQuestion &question, const IPAddress &resolvedIp,
                      uint32_t ttlNetOrder) {
  AsyncUDPMessage reply;
  header.flags = htons(0x8000U | (ntohs(header.flags) & 0x7800U));
  header.anCount = header.qdCount;
  reply.write(reinterpret_cast<unsigned char *>(&header), kDnsHeaderSize);
  reply.write(question.qName, question.qNameLength);
  reply.write(reinterpret_cast<uint8_t *>(&question.qType), 2);
  reply.write(reinterpret_cast<uint8_t *>(&question.qClass), 2);

  reply.write(static_cast<uint8_t>(0xC0));
  reply.write(static_cast<uint8_t>(kDnsOffsetDomainName));

  uint16_t answerType = htons(kDnsTypeA);
  uint16_t answerClass = htons(kDnsClassIn);
  uint16_t rdLength = htons(4);
  reply.write(reinterpret_cast<unsigned char *>(&answerType), 2);
  reply.write(reinterpret_cast<unsigned char *>(&answerClass), 2);
  reply.write(reinterpret_cast<unsigned char *>(&ttlNetOrder), 4);
  reply.write(reinterpret_cast<unsigned char *>(&rdLength), 2);

  uint32_t ip = static_cast<uint32_t>(resolvedIp);
  reply.write(reinterpret_cast<uint8_t *>(&ip), sizeof(ip));

  udp.sendTo(reply, req.remoteIP(), req.remotePort());
}

}  // namespace

class CaptiveApDnsServer::Impl {
 public:
  AsyncUDP udp;
  IPAddress apIp;
  IPAddress listenIp;
  uint32_t ttlNetOrder = htonl(kDefaultTtl);
};

bool CaptiveApDnsServer::start(const IPAddress &apIp,
                               const IPAddress &listenIp) {
  stop();

  _impl = new Impl();
  _impl->apIp = apIp;
  _impl->listenIp = listenIp;

  _impl->udp.onPacket([this](AsyncUDPPacket &pkt) {
    if (!_impl) return;

    if (pkt.length() < kDnsMinReqLen) return;

    const IPAddress localIp = pkt.localIP();
    if (localIp != _impl->listenIp && localIp != _impl->apIp) {
      return;
    }

    SetupDnsPolicy::noteApDnsRequest();

    DnsHeader header{};
    memcpy(&header, pkt.data(), kDnsHeaderSize);
    if (!isQuery(header)) return;

    DnsQuestion question{};
    if (!requestIncludesOnlyOneQuestion(header)) return;

    const char *endOfLabels =
        strchr(reinterpret_cast<const char *>(pkt.data()) + kDnsHeaderSize, 0);
    if (!endOfLabels) return;
    ++endOfLabels;

    question.qName = pkt.data() + kDnsHeaderSize;
    question.qNameLength =
        static_cast<uint16_t>(endOfLabels - reinterpret_cast<const char *>(pkt.data()) -
                              kDnsHeaderSize);
    if (question.qNameLength >
        pkt.length() - kDnsHeaderSize - sizeof(question.qType) -
            sizeof(question.qClass)) {
      return;
    }

    memcpy(&question.qType, endOfLabels, sizeof(question.qType));
    memcpy(&question.qClass, endOfLabels + sizeof(question.qType),
           sizeof(question.qClass));

    const uint16_t qType = ntohs(question.qType);
    if (qType != kDnsTypeA && qType != 255) return;

    sendARecordReply(_impl->udp, pkt, header, question, _impl->apIp,
                     _impl->ttlNetOrder);
    SetupDnsPolicy::noteApDnsLocalResponse();
  });

  const bool ok = _impl->udp.listen(kDnsPort);
  _running = ok;
  return ok;
}

void CaptiveApDnsServer::stop() {
  if (_impl) {
    _impl->udp.close();
    delete _impl;
    _impl = nullptr;
  }
  _running = false;
}

CaptiveApDnsServer::~CaptiveApDnsServer() { stop(); }
