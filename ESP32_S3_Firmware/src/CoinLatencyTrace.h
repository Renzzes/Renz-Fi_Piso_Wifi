#pragma once

#include <Arduino.h>

/**
 * Forensic-only monotonic timing for coin → SSE latency.
 * Does not change behavior. Uses millis(); never wall clock.
 * No SD writes. Serial summary only (rate-limited to one line per coin).
 *
 * T0 = first pulse of group (ISR-adjacent)
 * T1 = pulse group finalized
 * T2 = credit applied to portal RAM
 * T3 = promo resolution completed
 * T4 = EmitSessionEvent queued
 * T5 = EmitSessionEvent executed
 * T6 = EventBus/SSE emit completed
 * T7–T9 = browser (portal JS console)
 */
struct CoinLatencyTrace {
  volatile uint32_t t0Ms = 0;
  uint32_t t1Ms = 0;
  uint32_t t2Ms = 0;
  uint32_t t3Ms = 0;
  uint32_t t4Ms = 0;
  uint32_t t5Ms = 0;
  uint32_t t6Ms = 0;
  int pesoAmount = 0;
  char mac[24] = {0};
  bool armed = false;

  void markT0FromIsr() {
    if (t0Ms == 0) t0Ms = millis();
  }

  void markT1Finalized(int pesos) {
    t1Ms = millis();
    pesoAmount = pesos;
    armed = true;
  }

  void markT2CreditApplied(const String &sessionMac) {
    t2Ms = millis();
    sessionMac.toCharArray(mac, sizeof(mac));
  }

  void markT3PromoDone() { t3Ms = millis(); }

  void markT4Queued() { t4Ms = millis(); }

  void markT5Emitted() { t5Ms = millis(); }

  void markT6SseSent() {
    t6Ms = millis();
    if (!armed) return;
    const uint32_t t0 = t0Ms;
    // Note: promo (T3) runs before credit apply (T2) in current source order.
    Serial.printf(
        "[coin-latency] mac=%s peso=%d "
        "T0=%u T1=%u T3_promo=%u T2_credit=%u T4_q=%u T5_emit=%u T6_sse=%u "
        "dSettle=%d dPromo=%d dCreditAfterPromo=%d dQueueWait=%d dEmit=%d "
        "dT0_T2=%d dT0_T6=%d\n",
        mac[0] ? mac : "-",
        pesoAmount,
        (unsigned)t0, (unsigned)t1Ms, (unsigned)t3Ms, (unsigned)t2Ms,
        (unsigned)t4Ms, (unsigned)t5Ms, (unsigned)t6Ms,
        (int)(t1Ms - t0), (int)(t3Ms - t1Ms), (int)(t2Ms - t3Ms),
        (int)(t5Ms - t4Ms), (int)(t6Ms - t5Ms),
        (int)(t2Ms - t0), (int)(t6Ms - t0));
    reset();
  }

  void reset() {
    t0Ms = 0;
    t1Ms = t2Ms = t3Ms = t4Ms = t5Ms = t6Ms = 0;
    pesoAmount = 0;
    mac[0] = '\0';
    armed = false;
  }
};

inline CoinLatencyTrace &coinLatencyTrace() {
  static CoinLatencyTrace trace;
  return trace;
}
