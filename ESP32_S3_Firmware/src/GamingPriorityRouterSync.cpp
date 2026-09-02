#include "GamingPriorityRouterSync.h"

#include "Config.h"
#include "GamingPriorityTypes.h"
#include "JsonHeap.h"
#include "RouterCommandScratch.h"
#include "RouterOsClient.h"

namespace {

bool gpRun(RouterOsClient &client, const char *path, uint8_t count,
           const char *const *attrs, RouterOsClient::CommandResult &result) {
  String converted[12];
  if (count > 12) return false;
  for (uint8_t i = 0; i < count; ++i) converted[i] = attrs[i];
  return client.executeCommand(path, converted, count, result) &&
         !result.trapReceived;
}

bool gpRemoveOwnedMangle(RouterOsClient &client,
                         RouterOsClient::CommandResult &result) {
  const char *printAttrs[] = {"=brief", "=.proplist=.id,comment"};
  if (!gpRun(client, "/ip/firewall/mangle/print", 2, printAttrs, result)) {
    return false;
  }
  char idAttr[24];
  const char *removeAttrs[1];
  char commentBuf[48];
  char idBuf[16];
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    if (!RouterOsClient::replyAttrToBuf(result, i, "comment", commentBuf, sizeof(commentBuf))) {
      continue;
    }
    if (strncmp(commentBuf, "renzfi-gp:", 10) != 0) continue;
    RouterOsClient::replyAttrToBuf(result, i, ".id", idBuf, sizeof(idBuf));
    snprintf(idAttr, sizeof(idAttr), "=.id=%s", idBuf);
    removeAttrs[0] = idAttr;
    if (!gpRun(client, "/ip/firewall/mangle/remove", 1, removeAttrs, result)) {
      return false;
    }
  }
  return true;
}

bool gpRemoveOwnedNamed(RouterOsClient &client, const char *printPath,
                        const char *removePath,
                        RouterOsClient::CommandResult &result) {
  const char *printAttrs[] = {"=brief", "=.proplist=.id,name"};
  if (!gpRun(client, printPath, 2, printAttrs, result)) return false;
  char idAttr[24];
  const char *removeAttrs[1];
  char nameBuf[48];
  char idBuf[16];
  for (uint8_t i = 0; i < result.replyCount; ++i) {
    if (!RouterOsClient::replyAttrToBuf(result, i, "name", nameBuf, sizeof(nameBuf))) continue;
    if (strncmp(nameBuf, GamingPriority::kOwnerPrefix,
                strlen(GamingPriority::kOwnerPrefix)) != 0) {
      continue;
    }
    RouterOsClient::replyAttrToBuf(result, i, ".id", idBuf, sizeof(idBuf));
    snprintf(idAttr, sizeof(idAttr), "=.id=%s", idBuf);
    removeAttrs[0] = idAttr;
    if (!gpRun(client, removePath, 1, removeAttrs, result)) return false;
  }
  return true;
}

bool gpFindNamedId(RouterOsClient &client, const char *printPath,
                   const char *name, char *idOut, size_t idCap,
                   RouterOsClient::CommandResult &result) {
  if (!idOut || idCap == 0) return false;
  idOut[0] = '\0';
  char query[64];
  snprintf(query, sizeof(query), "?name=%s", name);
  const char *printAttrs[] = {query, "=.proplist=.id,name"};
  if (!gpRun(client, printPath, 2, printAttrs, result) ||
      result.replyCount == 0) {
    return false;
  }
  return RouterOsClient::replyAttrToBuf(result, 0, ".id", idOut, idCap) && idOut[0] != '\0';
}

bool gpUpsertNamed(RouterOsClient &client, const char *printPath,
                   const char *addPath, const char *setPath, const char *name,
                   uint8_t addCount, const char *const *addAttrs,
                   uint8_t setCount, const char *const *setAttrs,
                   RouterOsClient::CommandResult &result) {
  char idBuf[16];
  if (!gpFindNamedId(client, printPath, name, idBuf, sizeof(idBuf), result)) {
    idBuf[0] = '\0';
  }
  if (!idBuf[0]) {
    return gpRun(client, addPath, addCount, addAttrs, result);
  }
  char idAttr[24];
  snprintf(idAttr, sizeof(idAttr), "=.id=%s", idBuf);
  char setBuf[8][96];
  const char *merged[9];
  uint8_t count = 0;
  merged[count++] = idAttr;
  for (uint8_t i = 0; i < setCount && count < 9; ++i) {
    strncpy(setBuf[i], setAttrs[i], sizeof(setBuf[i]) - 1);
    setBuf[i][sizeof(setBuf[i]) - 1] = '\0';
    merged[count++] = setBuf[i];
  }
  return gpRun(client, setPath, count, merged, result);
}

}  // namespace

bool gamingPriorityRouterSync(RouterOsClient &client, const String &requestJson,
                              const String &guestBridge, String &messageOut,
                              String &errorOut) {
  HeapJsonDocument bodyDoc(RenzFiConfig::JSON_DOC_SMALL);
  DynamicJsonDocument &body = bodyDoc.doc();
  if (deserializeJson(body, requestJson)) {
    errorOut = "Invalid GP payload";
    return false;
  }
  RouterOsClient::CommandResult &result = RouterCommandScratchContext::acquire();
  const bool enabled = body["enabled"] | false;
  if (!enabled) {
    if (!gpRemoveOwnedMangle(client, result) ||
        !gpRemoveOwnedNamed(client, "/queue/tree/print", "/queue/tree/remove",
                            result) ||
        !gpRemoveOwnedNamed(client, "/queue/type/print", "/queue/type/remove",
                            result)) {
      errorOut = result.trapMessage.length() > 0 ? result.trapMessage
                                                 : "Unable to disable GP";
      return false;
    }
    messageOut = "Gaming Priority disabled";
    return true;
  }

  const uint16_t maximum = body["maximumGamingMbps"] | 0;
  const uint16_t perUser = body["perUserGamingMbps"] | 0;
  if (maximum == 0 || perUser == 0 || guestBridge.isEmpty()) {
    errorOut = "GP bandwidth or bridge missing";
    return false;
  }
  char maxLimit[12];
  char pcqRate[12];
  char prioStr[4];
  snprintf(maxLimit, sizeof(maxLimit), "%uM", static_cast<unsigned>(maximum));
  snprintf(pcqRate, sizeof(pcqRate), "%uM", static_cast<unsigned>(perUser));
  snprintf(prioStr, sizeof(prioStr), "%u",
           static_cast<unsigned>(GamingPriority::routerOsQueuePriority(
               body["priority"] | "normal")));

  char mangleIds[16][8];
  char mangleComments[16][48];
  uint8_t mangleCount = 0;
  {
    const char *printAttrs[] = {"=brief", "=.proplist=.id,comment"};
    if (gpRun(client, "/ip/firewall/mangle/print", 2, printAttrs, result)) {
      for (uint8_t i = 0; i < result.replyCount && mangleCount < 16; ++i) {
        char commentBuf[48];
        if (!RouterOsClient::replyAttrToBuf(result, i, "comment", commentBuf, sizeof(commentBuf))) {
          continue;
        }
        if (strncmp(commentBuf, "renzfi-gp:", 10) != 0) continue;
        strncpy(mangleComments[mangleCount], commentBuf,
                sizeof(mangleComments[mangleCount]) - 1);
        RouterOsClient::replyAttrToBuf(result, i, ".id", mangleIds[mangleCount],
                    sizeof(mangleIds[mangleCount]));
        mangleCount++;
      }
    }
  }

  JsonArrayConst profiles = body["gameProfiles"].as<JsonArrayConst>();
  if (!profiles.isNull()) {
    char attrBuf[10][96];
    const char *attrs[10];
    for (JsonObjectConst row : profiles) {
      if (!(row["enabled"] | false)) continue;
      if (strcmp(row["classificationMethod"] | "", GamingPriority::kClassMethod) !=
          0) {
        continue;
      }
      JsonObjectConst data = row["classificationData"].as<JsonObjectConst>();
      const char *slug = row["slug"] | "";
      const char *protocol = data["protocol"] | "udp";
      const char *ports = data["ports"] | "";
      if (!slug[0] || !ports[0]) continue;
      char comment[48];
      snprintf(comment, sizeof(comment), "renzfi-gp:%s", slug);
      const char *ruleId = nullptr;
      for (uint8_t i = 0; i < mangleCount; ++i) {
        if (strcmp(mangleComments[i], comment) == 0) {
          ruleId = mangleIds[i];
          break;
        }
      }
      uint8_t count = 0;
      if (ruleId && ruleId[0]) {
        snprintf(attrBuf[count], sizeof(attrBuf[count]), "=.id=%s", ruleId);
        attrs[count++] = attrBuf[count - 1];
      }
      attrs[count++] = "=chain=forward";
      snprintf(attrBuf[count], sizeof(attrBuf[count]), "=in-interface=%s",
               guestBridge.c_str());
      attrs[count++] = attrBuf[count - 1];
      snprintf(attrBuf[count], sizeof(attrBuf[count]), "=protocol=%s", protocol);
      attrs[count++] = attrBuf[count - 1];
      snprintf(attrBuf[count], sizeof(attrBuf[count]), "=dst-port=%s", ports);
      attrs[count++] = attrBuf[count - 1];
      attrs[count++] = "=action=mark-connection";
      snprintf(attrBuf[count], sizeof(attrBuf[count]),
               "=new-connection-mark=%s", GamingPriority::kConnMark);
      attrs[count++] = attrBuf[count - 1];
      attrs[count++] = "=passthrough=yes";
      snprintf(attrBuf[count], sizeof(attrBuf[count]), "=comment=%s", comment);
      attrs[count++] = attrBuf[count - 1];
      const char *path =
          (ruleId && ruleId[0]) ? "/ip/firewall/mangle/set"
                                : "/ip/firewall/mangle/add";
      if (!gpRun(client, path, count, attrs, result)) {
        errorOut = result.trapMessage.length() > 0 ? result.trapMessage
                                                   : "Unable to apply mangle";
        return false;
      }
    }
  }

  const char *pktComment = "renzfi-gp:pkt-mark";
  const char *pktId = nullptr;
  for (uint8_t i = 0; i < mangleCount; ++i) {
    if (strcmp(mangleComments[i], pktComment) == 0) {
      pktId = mangleIds[i];
      break;
    }
  }
  if (!pktId || !pktId[0]) {
    char pktAttrs[6][96];
    const char *addPkt[6];
    addPkt[0] = "=chain=forward";
    snprintf(pktAttrs[1], sizeof(pktAttrs[1]), "=connection-mark=%s",
             GamingPriority::kConnMark);
    addPkt[1] = pktAttrs[1];
    addPkt[2] = "=action=mark-packet";
    snprintf(pktAttrs[3], sizeof(pktAttrs[3]), "=new-packet-mark=%s",
             GamingPriority::kPktMark);
    addPkt[3] = pktAttrs[3];
    addPkt[4] = "=passthrough=yes";
    snprintf(pktAttrs[5], sizeof(pktAttrs[5]), "=comment=%s", pktComment);
    addPkt[5] = pktAttrs[5];
    if (!gpRun(client, "/ip/firewall/mangle/add", 6, addPkt, result)) {
      errorOut = "Unable to apply packet mark";
      return false;
    }
  }

  {
    char pcqAdd[5][96];
    const char *addPcq[5];
    snprintf(pcqAdd[0], sizeof(pcqAdd[0]), "=name=%s", GamingPriority::kPcqDownload);
    addPcq[0] = pcqAdd[0];
    addPcq[1] = "=kind=pcq";
    snprintf(pcqAdd[2], sizeof(pcqAdd[2]), "=pcq-rate=%s", pcqRate);
    addPcq[2] = pcqAdd[2];
    addPcq[3] = "=pcq-classifier=dst-address";
    addPcq[4] = "=comment=renzfi-gp:pcq-download";
    char setPcq[1][96];
    const char *setAttrs[1];
    snprintf(setPcq[0], sizeof(setPcq[0]), "=pcq-rate=%s", pcqRate);
    setAttrs[0] = setPcq[0];
    if (!gpUpsertNamed(client, "/queue/type/print", "/queue/type/add",
                       "/queue/type/set", GamingPriority::kPcqDownload, 5,
                       addPcq, 1, setAttrs, result)) {
      errorOut = "Unable to apply PCQ";
      return false;
    }
  }

  {
    char qtAdd[7][96];
    const char *addQt[7];
    snprintf(qtAdd[0], sizeof(qtAdd[0]), "=name=%s", GamingPriority::kQtDownload);
    addQt[0] = qtAdd[0];
    addQt[1] = "=parent=global";
    snprintf(qtAdd[2], sizeof(qtAdd[2]), "=packet-mark=%s",
             GamingPriority::kPktMark);
    addQt[2] = qtAdd[2];
    snprintf(qtAdd[3], sizeof(qtAdd[3]), "=queue=%s", GamingPriority::kPcqDownload);
    addQt[3] = qtAdd[3];
    snprintf(qtAdd[4], sizeof(qtAdd[4]), "=priority=%s", prioStr);
    addQt[4] = qtAdd[4];
    snprintf(qtAdd[5], sizeof(qtAdd[5]), "=max-limit=%s", maxLimit);
    addQt[5] = qtAdd[5];
    addQt[6] = "=comment=renzfi-gp:qt-download";
    char setQt[2][96];
    const char *setAttrs[2];
    snprintf(setQt[0], sizeof(setQt[0]), "=priority=%s", prioStr);
    setAttrs[0] = setQt[0];
    snprintf(setQt[1], sizeof(setQt[1]), "=max-limit=%s", maxLimit);
    setAttrs[1] = setQt[1];
    if (!gpUpsertNamed(client, "/queue/tree/print", "/queue/tree/add",
                       "/queue/tree/set", GamingPriority::kQtDownload, 7, addQt,
                       2, setAttrs, result)) {
      errorOut = "Unable to apply queue tree";
      return false;
    }
  }

  messageOut = "Gaming Priority applied";
  return true;
}
