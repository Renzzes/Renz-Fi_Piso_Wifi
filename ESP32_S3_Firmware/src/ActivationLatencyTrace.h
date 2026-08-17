#pragma once

#include <Arduino.h>

/**
 * Lightweight DONE PAYING → CONNECTED timing (millis only).
 * One Serial summary line per activation. No SD. No credentials.
 *
 * T0  donePaying() entered
 * T1  payment/session validation complete
 * T2  entitlement reserved (Activating, secondsLeft set)
 * T3  RouterWorker job accepted (or portal ActivateSession queued)
 * T4  RouterWorker began ActivateHotspotUser
 * T5  RouterOS API session ready (connect+login)
 * T6  authorization sequence started (user/print)
 * T7  hotspot user add/set done
 * T8  hotspot active login/set done
 * T9  worker published HotspotOutcome
 * T10 portal published portal.session.connected (SSE path)
 */
struct ActivationLatencyTrace {
  uint32_t t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
  uint32_t t5 = 0, t6 = 0, t7 = 0, t8 = 0, t9 = 0, t10 = 0;
  char mac[24] = {0};
  bool armed = false;

  void reset() {
    t0 = t1 = t2 = t3 = t4 = t5 = t6 = t7 = t8 = t9 = t10 = 0;
    mac[0] = '\0';
    armed = false;
  }

  void begin(const String &sessionMac) {
    reset();
    t0 = millis();
    sessionMac.toCharArray(mac, sizeof(mac));
    armed = true;
  }

  void markT1() { if (armed && t1 == 0) t1 = millis(); }
  void markT2() { if (armed && t2 == 0) t2 = millis(); }
  void markT3() { if (armed && t3 == 0) t3 = millis(); }
  void markT4() { if (armed && t4 == 0) t4 = millis(); }
  void markT5() { if (armed && t5 == 0) t5 = millis(); }
  void markT6() { if (armed && t6 == 0) t6 = millis(); }
  void markT7() { if (armed && t7 == 0) t7 = millis(); }
  void markT8() { if (armed && t8 == 0) t8 = millis(); }
  void markT9() { if (armed && t9 == 0) t9 = millis(); }

  void finishT10() {
    if (!armed) return;
    t10 = millis();
    const uint32_t z = t0;
    Serial.printf(
        "[activate-latency] mac=%s "
        "coinAt=%u donePayingAt=%u portalReserveAt=%u activateEnqueueAt=%u "
        "workerStartAt=%u routerConnectAt=%u routerAuthStartAt=%u "
        "userSetOrAddAt=%u activeLoginSuccessAt=%u workerOutcomeAt=%u "
        "portalCommitAt=%u "
        "enqueue=%d workerQ=%d rosLogin=%d rosAuth=%d resultPub=%d "
        "total_esp=%d\n",
        mac[0] ? mac : "-",
        (unsigned)t0, (unsigned)t1, (unsigned)t2, (unsigned)t3, (unsigned)t4,
        (unsigned)t5, (unsigned)t6, (unsigned)t7, (unsigned)t8, (unsigned)t9,
        (unsigned)t10,
        (int)(t3 - z), (int)(t4 - t3), (int)(t5 - t4), (int)(t8 - t6),
        (int)(t10 - t9), (int)(t10 - z));
    reset();
  }
};

inline ActivationLatencyTrace &activationLatencyTrace() {
  static ActivationLatencyTrace trace;
  return trace;
}
