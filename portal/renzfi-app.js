(function () {
  "use strict";

  // ── configuration ──────────────────────────────────────────────────────────
  // Canonical appliance base URL resolver. This portal bundle is deployed to
  // MikroTik Hotspot storage (a different origin than the ESP32 API), so the
  // base URL below is the single place that decides where API calls go:
  //
  //   A/B. deployment/mikrotik-hotspot/renzfi-app.js has this placeholder
  //        replaced at build time by scripts/build-mikrotik-portal.mjs — that
  //        value is always used when present.
  //   C.   If the placeholder was never replaced, this copy is being served
  //        directly by the ESP32 itself (development/local-recovery fallback
  //        only — see PortalServer). Same-origin is safe here because the
  //        MikroTik build step always substitutes the placeholder; the
  //        browser hostname is never used to *guess* the ESP32 address.
  var RENZFI_APPLIANCE_BASE_URL = "__RENZFI_APPLIANCE_BASE_URL__";

  function resolveApplianceBaseUrl() {
    var configured = RENZFI_APPLIANCE_BASE_URL;
    var isPlaceholder = !configured ||
      String(configured).indexOf("__RENZFI_APPLIANCE_BASE_URL__") !== -1;
    if (!isPlaceholder) {
      var trimmed = String(configured).replace(/\/+$/, "");
      if (/^https?:\/\/.+/i.test(trimmed)) return trimmed;
    }
    // Development/recovery only — ESP32 serves portal/ from the same origin.
    return window.location.origin.replace(/\/+$/, "");
  }

  var APPLIANCE_BASE_URL = resolveApplianceBaseUrl();
  var API_BASE           = APPLIANCE_BASE_URL + "/api/portal";
  var BRANDING_BASE      = APPLIANCE_BASE_URL;
  var EVENTS_BASE        = APPLIANCE_BASE_URL;
  var STORAGE_PREFIX    = "renzFiPortalState";
  var INSERT_TIMEOUT    = 60;
  var HEARTBEAT_MS      = 10000;
  var COIN_POLL_MS      = 2000;

  // Consecutive background-request failures before showing the
  // "service temporarily unavailable" notice (see noteApplianceFailure()).
  var SERVICE_UNAVAILABLE_THRESHOLD = 2;

  // Timer ownership: the ESP32 owns the expiry timeline
  // (authorizedAtMs + grantedSeconds = expiresAtMs). The browser is
  // presentation only. A GET/SSE snapshot must never increase remaining
  // time. Only a newer sessionGeneration or a larger grantedSeconds
  // (Add Time / new purchase) may move the deadline later.

  // ── timers ─────────────────────────────────────────────────────────────────
  var mainTimer      = null;
  var coinTimer      = null;
  var coinPollTimer  = null;
  var heartbeatTimer = null;

  // Monotonic generation for GET /session — overlapping responses must not
  // apply out of order (countdown rebase / coin-window races).
  var sessionSyncGen = 0;

  // Countdown anchors — the only place elapsed time is tracked in the browser.
  var sessionExpiryAt      = 0;     // wall-clock instant the session runs out
  var sessionFrozenSeconds = 0;     // value to show while the clock is stopped
  var sessionTimerRunning  = false; // firmware says the countdown is advancing
  var coinAnchorSeconds    = 0;
  var coinAnchorAt         = 0;

  // Coin modal lifecycle guards — prevent duplicate cancel/poll/timer requests.
  var coinModalVisible     = false;
  var coinCancelInFlight   = false;
  var donePayingInFlight   = false;
  var coinTimeoutHandled   = false;
  var lastWindowInserted   = 0;
  var sessionNoticeTicks   = 0;
  var warnedAt30Seconds    = false;
  var warnedAt15Seconds    = false;

  // ── device identification ──────────────────────────────────────────────────
  function textOf(id) {
    var el = document.getElementById(id);
    return el ? el.textContent.trim() : "";
  }

  function getDeviceMAC() {
    var mac = textOf("macAddress");
    return mac && mac.indexOf("$(") === -1 ? mac : "";
  }

  function getDeviceIP() {
    var ip = textOf("ipAddress");
    return ip && ip.indexOf("$(") === -1 ? ip : "";
  }

  function getDeviceKey() {
    var mac = getDeviceMAC();
    var ip  = getDeviceIP();
    return (mac || ip || "unknown-device").replace(/[^a-z0-9._:-]/gi, "_");
  }

  function deviceParams() {
    return { mac: getDeviceMAC(), ip: getDeviceIP() };
  }

  // ── ESP32 API contract adapters ────────────────────────────────────────────
  function unwrapPortalResponse(json) {
    if (!json || json.success !== true) {
      var msg = (json && (json.error || json.message))
        ? (json.error || json.message)
        : "API request failed";
      var err = new Error(msg);
      err.code = (json && json.code) || "";
      err.status = (json && json.status) || 0;
      throw err;
    }
    return json.data;
  }

  function normalizeSession(raw) {
    if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;

    var coinActive = raw.coinSessionActive;
    if (coinActive === undefined) coinActive = raw.coin_session_active;
    if (coinActive === undefined) coinActive = raw.coinWindowActive;
    if (coinActive === undefined) coinActive = raw.coin_window_active;

    var coinCountdown = raw.coinCountdown;
    if (coinCountdown === undefined) coinCountdown = raw.coin_countdown;
    if (coinCountdown === undefined) coinCountdown = raw.coinWindowRemaining;
    if (coinCountdown === undefined) coinCountdown = raw.coin_window_remaining;

    var stateStr = raw.sessionState || raw.session_state || "";
    var source = raw.source || raw.session_source || "portal";
    var secondsLeft = Number(raw.secondsLeft || raw.seconds_left) || 0;
    var paused = Boolean(raw.paused);

    return {
      secondsLeft:       secondsLeft,
      // Firmware-declared: is the countdown advancing right now? Older builds
      // that omit the flag fall back to the equivalent state test.
      timerRunning:      raw.timerRunning !== undefined
                            ? Boolean(raw.timerRunning)
                            : (stateStr === "active" && !paused && secondsLeft > 0),
      credits:           Number(raw.credits)                                    || 0,
      paused:            paused,
      connected:         Boolean(raw.connected),
      coinSessionActive: Boolean(coinActive),
      // Allow 0 (window expired). `Number(0) || INSERT_TIMEOUT` wrongly resets to 60.
      coinCountdown:     (coinCountdown === undefined || coinCountdown === null)
                            ? INSERT_TIMEOUT
                            : Math.max(0, Number(coinCountdown) || 0),
      insertedAmount:    Number(raw.insertedAmount   || raw.inserted_amount)    || 0,
      purchasedMinutes:  Number(raw.purchasedMinutes || raw.purchased_minutes)  || 0,
      sessionState:      stateStr,
      activationError:   Boolean(raw.activationError || raw.activation_error),
      activationErrorReason: raw.activationErrorReason ||
                             raw.activation_error_reason || "",
      pausesRemaining:   raw.pausesRemaining !== undefined
                            ? Number(raw.pausesRemaining)
                            : null,
      pauseLimit:        Number(raw.pauseLimit) || 0,
      source:             source,
      voucherCode:        raw.voucherCode || raw.voucher_code || "",
      voucherStatus:      raw.voucherStatus || raw.voucher_status || "",
      voucherExpiresAt:   raw.voucherExpiresAt || raw.voucher_expires_at || "",
      canInsertCoin:      raw.canInsertCoin !== undefined
                            ? Boolean(raw.canInsertCoin)
                            : source !== "voucher",
      canPause:           raw.canPause !== undefined
                            ? Boolean(raw.canPause)
                            : source !== "voucher",
      canResume:          raw.canResume !== undefined
                            ? Boolean(raw.canResume)
                            : source !== "voucher",
      canTerminate:       raw.canTerminate !== undefined
                            ? Boolean(raw.canTerminate)
                            : source !== "voucher",
      canReconnect:       raw.canReconnect !== undefined
                            ? Boolean(raw.canReconnect)
                            : source === "voucher",
      sessionGeneration:  Number(raw.sessionGeneration || raw.session_generation) || 0,
      grantedSeconds:     Number(raw.grantedSeconds || raw.granted_seconds) || 0,
      authorizedAtMs:     Number(raw.authorizedAtMs || raw.authorized_at_ms) || 0,
      expiresAtMs:        Number(raw.expiresAtMs || raw.expires_at_ms) || 0,
      serverNowMs:        Number(raw.serverNowMs || raw.server_now_ms) || 0
    };
  }

  // Issue 5: richer promo normalizer — passes through speedProfile, deviceLimit, enabled.
  function normalizeRatesPayload(rawData) {
    var promos = Array.isArray(rawData) ? rawData : (rawData && rawData.rates);
    if (!Array.isArray(promos)) return null;

    var rates = promos.filter(function (p) {
      // Skip disabled promos (Issue 5)
      if (p.enabled === false || p.enabled === "false") return false;
      var peso    = Number(p.coin != null ? p.coin    : p.peso)    || 0;
      var minutes = Number(p.minutes) || 0;
      return peso > 0 && minutes > 0;
    }).map(function (p) {
      var peso    = Number(p.coin != null ? p.coin    : p.peso)    || 0;
      var minutes = Number(p.minutes) || 0;
      return {
        peso:         peso,
        minutes:      minutes,
        speedProfile: p.speedProfile || p.speed_profile || "",
        deviceLimit:  Number(p.deviceLimit || p.device_limit) || 0,
        enabled:      p.enabled !== false && p.enabled !== "false"
      };
    });

    rates.sort(function (a, b) { return a.peso - b.peso; });
    return { rates: rates };
  }

  // ── localStorage (UI cache only) ───────────────────────────────────────────
  var storageKey = STORAGE_PREFIX + ":device";

  function defaultState() {
    return {
      credits:            0,
      secondsLeft:        0,
      paused:             false,
      connected:          false,
      coinCountdown:      INSERT_TIMEOUT,
      coinSessionActive:  false,
      insertedAmount:     0,
      purchasedMinutes:   0,
      sessionState:       "idle",
      timerRunning:       false,
      activationError:    false,
      activationErrorReason: "",
      pausesRemaining:    null,
      pauseLimit:         0,
      source:             "portal",
      voucherCode:        "",
      voucherStatus:      "",
      voucherExpiresAt:   "",
      canInsertCoin:      true,
      canPause:           true,
      canResume:          true,
      canTerminate:       true,
      canReconnect:       false,
      sessionGeneration:  0,
      grantedSeconds:     0,
      // Per-session coin modal accumulators — always reset to zero when modal opens
      coinAmount:         0,
      coinMinutes:        0,
      coinVoucherMinutes: 0,
      coinPulseCount:     0,
      coinBaseCredits:    0
    };
  }

  function loadCachedState() {
    try {
      var saved = JSON.parse(localStorage.getItem(storageKey) || "{}");
      return {
        credits:            Number(saved.credits)           || 0,
        secondsLeft:        Number(saved.secondsLeft)       || 0,
        // Never resume ticking from cache — the countdown stays frozen on the
        // last known value until the firmware confirms it.
        timerRunning:       false,
        paused:             Boolean(saved.paused),
        coinCountdown:      Number(saved.coinCountdown)     || INSERT_TIMEOUT,
        coinSessionActive:  Boolean(saved.coinSessionActive),
        insertedAmount:     Number(saved.insertedAmount)    || 0,
        sessionState:       saved.sessionState              || "idle",
        source:             saved.source                    || "portal",
        voucherCode:        saved.voucherCode               || "",
        voucherStatus:      saved.voucherStatus             || "",
        voucherExpiresAt:   saved.voucherExpiresAt          || "",
        canInsertCoin:      saved.canInsertCoin !== false,
        canPause:           saved.canPause !== false,
        canResume:          saved.canResume !== false,
        canTerminate:       saved.canTerminate !== false,
        canReconnect:       Boolean(saved.canReconnect),
        // Never restore coin-session accumulators from cache
        coinAmount:         0,
        coinMinutes:        0,
        coinVoucherMinutes: 0,
        coinPulseCount:     0,
        coinBaseCredits:    0
      };
    } catch (e) {
      return defaultState();
    }
  }

  function saveStateCache() {
    localStorage.setItem(storageKey, JSON.stringify(state));
  }

  var state = defaultState();

  // ── countdown derivation ───────────────────────────────────────────────────
  // Presentation only. Remaining is derived from the firmware expiry
  // snapshot. The browser never becomes the authority for entitlement,
  // Internet authorization, or session generation.

  function serverRemainingOf(session) {
    var left = Math.max(0, Number(session.secondsLeft) || 0);
    var expiresAt = Number(session.expiresAtMs) || 0;
    var serverNow = Number(session.serverNowMs) || 0;
    if (expiresAt > 0 && serverNow > 0) {
      var fromClock = Math.max(0, Math.floor((expiresAt - serverNow) / 1000));
      if (left <= 0 || fromClock <= left) return fromClock;
    }
    return left;
  }

  function applySessionClock(session, previousGen, previousGranted) {
    var incomingLeft = serverRemainingOf(session);
    var incomingGen = Number(session.sessionGeneration) || 0;
    var incomingGranted = Number(session.grantedSeconds) || 0;
    var running = Boolean(session.timerRunning) && incomingLeft > 0 &&
                  Boolean(session.connected) && !session.paused;

    if (!running) {
      sessionTimerRunning  = false;
      sessionFrozenSeconds = incomingLeft;
      sessionExpiryAt      = 0;
      return incomingLeft;
    }

    var legitimateIncrease =
      (incomingGen > 0 && incomingGen > (Number(previousGen) || 0)) ||
      (incomingGranted > 0 && incomingGranted > (Number(previousGranted) || 0));

    var candidate = Date.now() + incomingLeft * 1000;
    if (!sessionTimerRunning || sessionExpiryAt === 0) {
      sessionExpiryAt = candidate;
    } else if (legitimateIncrease) {
      sessionExpiryAt = candidate;
    } else if (candidate < sessionExpiryAt) {
      sessionExpiryAt = candidate;
    }

    sessionTimerRunning  = true;
    sessionFrozenSeconds = incomingLeft;
    return incomingLeft;
  }

  function anchorSession(seconds, running) {
    applySessionClock({
      secondsLeft: seconds,
      timerRunning: running,
      connected: running,
      paused: false,
      sessionGeneration: state.sessionGeneration,
      grantedSeconds: state.grantedSeconds,
      expiresAtMs: 0,
      serverNowMs: 0
    }, state.sessionGeneration, state.grantedSeconds);
  }

  function displaySeconds() {
    if (!sessionTimerRunning) return sessionFrozenSeconds;
    return Math.max(0, Math.ceil((sessionExpiryAt - Date.now()) / 1000));
  }

  function anchorCoinWindow(seconds) {
    coinAnchorSeconds = Math.max(0, Number(seconds) || 0);
    coinAnchorAt      = Date.now();
  }

  function derivedCoinSeconds() {
    if (!state.coinSessionActive) return INSERT_TIMEOUT;
    var elapsed = Math.floor((Date.now() - coinAnchorAt) / 1000);
    return Math.max(0, coinAnchorSeconds - Math.max(0, elapsed));
  }

  function applyNormalizedSession(session, trustFully) {
    if (!session) return;

    var incomingGen = Number(session.sessionGeneration) || 0;
    var currentGen = Number(state.sessionGeneration) || 0;
    if (!trustFully && incomingGen > 0 && currentGen > 0 && incomingGen < currentGen) {
      return;
    }

    // Credits, pause, coin state are always authoritative from server
    var hadCoinSession = state.coinSessionActive;
    var previousCredits = state.credits;
    var previousInserted = state.insertedAmount;
    var previousSeconds = state.secondsLeft;
    var previousSessionState = state.sessionState;
    var previousGen = currentGen;
    var previousGranted = Number(state.grantedSeconds) || 0;
    state.credits           = session.credits;
    state.paused            = session.paused;
    state.connected         = session.connected;
    state.coinSessionActive = session.coinSessionActive;
    state.insertedAmount    = session.insertedAmount;
    state.purchasedMinutes  = session.purchasedMinutes || 0;
    state.sessionState      = session.sessionState || state.sessionState;
    state.activationError   = Boolean(session.activationError);
    state.activationErrorReason = session.activationErrorReason || "";
    if (session.pausesRemaining !== null && session.pausesRemaining !== undefined) {
      state.pausesRemaining = session.pausesRemaining;
    }
    if (session.pauseLimit) state.pauseLimit = session.pauseLimit;
    state.source            = session.source || "portal";
    state.voucherCode       = session.voucherCode || "";
    state.voucherStatus     = session.voucherStatus || "";
    state.voucherExpiresAt  = session.voucherExpiresAt || "";
    state.canInsertCoin     = session.canInsertCoin;
    state.canPause          = session.canPause;
    state.canResume         = session.canResume;
    state.canTerminate      = session.canTerminate;
    state.canReconnect      = session.canReconnect;
    if (incomingGen > 0) state.sessionGeneration = incomingGen;
    if (session.grantedSeconds !== undefined && session.grantedSeconds !== null) {
      state.grantedSeconds = Number(session.grantedSeconds) || 0;
    }

    // Server closed the insert window — update UI only; never mutate server state.
    if (hadCoinSession && !session.coinSessionActive &&
        dom.coinModal && !dom.coinModal.hidden) {
      stopCoinSessionUI();
      closeModal(dom.coinModal, true);
    }

    // Prefer server window amount; fall back to credit delta when unavailable.
    if (state.coinSessionActive) {
      var windowAmt = Number(session.insertedAmount) || 0;
      if (windowAmt > 0 || session.insertedAmount === 0) {
        if (windowAmt > lastWindowInserted) playCoinSound();
        lastWindowInserted = windowAmt;
        state.coinAmount = windowAmt;
      } else {
        state.coinAmount = Math.max(0, session.credits - (state.coinBaseCredits || 0));
      }
      state.coinVoucherMinutes = Number(session.purchasedMinutes) || 0;

      // ESP32 owns coinWindowRemaining. Presentation uses a monotonic deadline so
      // a ~2s poll with a slightly older snapshot cannot rewind 55 → 56.
      // Legitimate resets: new coin (credits/insertedAmount up) or trustFully.
      var serverRem = Math.max(0, Number(session.coinCountdown) || 0);
      var presentRem = hadCoinSession ? derivedCoinSeconds() : serverRem;
      var newCoin = session.credits > previousCredits ||
                    windowAmt > previousInserted;
      var openedNow = !hadCoinSession && session.coinSessionActive;
      if (serverRem <= 0) {
        state.coinCountdown = 0;
        anchorCoinWindow(0);
      } else if (trustFully || newCoin || openedNow) {
        state.coinCountdown = serverRem;
        anchorCoinWindow(serverRem);
      } else {
        var adopt = Math.min(presentRem, serverRem);
        state.coinCountdown = adopt;
        anchorCoinWindow(adopt);
      }
    } else {
      state.coinCountdown = session.coinCountdown;
    }

    // Presentation clock: never increase remaining unless generation or
    // grantedSeconds increased. trustFully does not rebase a running timer.
    state.timerRunning  = session.timerRunning;
    state.secondsLeft   = applySessionClock(session, previousGen, previousGranted);

    var becameActive =
      previousSessionState !== "active" && state.sessionState === "active";
    var timeIncreased = state.secondsLeft > previousSeconds;
    if (becameActive || timeIncreased) {
      if (state.secondsLeft > 30) warnedAt30Seconds = false;
      if (state.secondsLeft > 15) warnedAt15Seconds = false;
      hideSessionNotice();
    }
    if (state.sessionState !== "active" || !state.connected ||
        state.secondsLeft <= 0 || state.paused) {
      hideSessionNotice();
    } else {
      updateSessionNotice(previousSeconds, state.secondsLeft);
    }

    saveStateCache();
    render();
  }

  function applySessionData(raw, trustFully) {
    applyNormalizedSession(normalizeSession(raw), trustFully);
  }

  // ── DOM refs ───────────────────────────────────────────────────────────────
  var dom = {};

  function cacheDom() {
    dom.insertCoinBtn      = document.getElementById("insertCoinBtn") ||
                             document.getElementById("insertCoinButton");
    dom.insertCoinLabel    = dom.insertCoinBtn &&
                             dom.insertCoinBtn.querySelector(".action-label");
    dom.pauseButton        = document.getElementById("pauseButton");
    dom.pauseButtonText    = document.getElementById("pauseButtonText");
    dom.terminateBtn       = document.getElementById("terminateBtn");
    dom.viewRatesBtn       = document.getElementById("viewRatesBtn") ||
                             document.getElementById("viewRatesButton");
    dom.coinModal          = document.getElementById("coinModal");
    dom.ratesModal         = document.getElementById("ratesModal");
    dom.terminateModal     = document.getElementById("terminateModal");
    dom.mainTimerEl        = document.getElementById("mainTimer");
    dom.creditsEl          = document.getElementById("credits");
    dom.currentDateEl      = document.getElementById("currentDate");
    dom.statusEl           = document.getElementById("connectionStatus");
    dom.coinCountdownEl    = document.getElementById("coinCountdown");
    dom.coinProgressBar    = document.getElementById("coinProgressBar");
    dom.coinTimeEl         = document.getElementById("coinTime");
    dom.coinVoucherTime    = document.getElementById("coinVoucherTime");
    dom.insertedAmountEl   = document.getElementById("insertedAmount");
    dom.coinNoteEl         = document.getElementById("coinNote");
    dom.donePayingBtn      = document.getElementById("donePayingBtn");
    dom.bgMusic            = document.getElementById("bgMusic");
    dom.coinSound          = document.getElementById("coinSound");
    dom.successSound       = document.getElementById("successSound");
    dom.ratesList          = document.querySelector("#ratesModal .rates-list");
    dom.serviceNotice      = document.getElementById("serviceNotice");
    dom.sessionNotice      = document.getElementById("sessionNotice");
    dom.voucherForm        = document.getElementById("voucherForm");
    dom.voucherCode        = document.getElementById("voucherCode");
    dom.voucherSubmitBtn   = document.getElementById("voucherSubmitBtn");
    dom.voucherHelp        = document.getElementById("voucherHelp");
    dom.voucherCard        = document.querySelector(".voucher-card");
    dom.terminateRemaining = document.getElementById("terminateRemaining");
    dom.terminateCredits   = document.getElementById("terminateCredits");
    dom.terminateStatus    = document.getElementById("terminateStatus");
    dom.confirmTerminateBtn = document.getElementById("confirmTerminateBtn");
    dom.cancelTerminateBtn  = document.getElementById("cancelTerminateBtn");
  }

  // ── HTTP helpers ───────────────────────────────────────────────────────────
  var brandingRevision    = 0;
  var brandingEventSource = null;

  // Every PortalSessionManager event that ships a full session payload. Each
  // one lets the portal re-render without an HTTP round trip; polling below
  // stays in place unchanged as the fallback.
  var SESSION_PUSH_EVENTS = [
    "portal.coin.started",
    "portal.coin.credit",
    "portal.coin.window_closed",
    "portal.session.updated",
    "portal.session.connected",
    "portal.session.paused",
    "portal.session.pause_failed",
    "portal.session.resumed",
    "portal.session.activation_failed",
    "portal.session.expired",
    "portal.session.terminated"
  ];

  function defaultBannerSrc() {
    var bannerEl = document.getElementById("portalBanner");
    return bannerEl
      ? (bannerEl.getAttribute("data-default-src") || "Default-Banner.png")
      : "Default-Banner.png";
  }

  function applyBranding(data) {
    if (!data) return;
    var bannerEl = document.getElementById("portalBanner");
    if (bannerEl) {
      var fallback = defaultBannerSrc();
      bannerEl.onerror = function () {
        bannerEl.onerror = null;
        bannerEl.src = fallback;
      };
      if (data.hasCustomBanner && data.bannerUrl) {
        bannerEl.src = data.bannerUrl;
      } else {
        bannerEl.src = fallback;
      }
    }
    if (dom.bgMusic) {
      var musicSrc = data.hasCustomMusic && data.musicUrl ? data.musicUrl : "bg_music.mp3";
      var current  = dom.bgMusic.getAttribute("src") || dom.bgMusic.currentSrc || "";
      if (current.indexOf(musicSrc) === -1) {
        dom.bgMusic.src = musicSrc;
        dom.bgMusic.load();
      }
    }
    brandingRevision = Number(data.revision) || 0;
  }

  function loadBranding() {
    return fetch(BRANDING_BASE + "/api/portal/branding", { cache: "no-store" })
      .then(function (r) {
        if (!r.ok) throw new Error("HTTP " + r.status);
        return r.json();
      })
      .then(function (json) { applyBranding(unwrapPortalResponse(json)); })
      .catch(function () {});
  }

  // One EventSource carries both branding and session pushes. Coin credit is
  // published by the ESP32 the moment the pulse is attributed, so the customer
  // sees it without waiting for the next poll — and it costs zero extra
  // RouterOS traffic because the stream is served by the appliance itself.
  function connectPortalEvents() {
    if (brandingEventSource) { brandingEventSource.close(); brandingEventSource = null; }
    try {
      brandingEventSource = new EventSource(EVENTS_BASE + "/api/events");
      brandingEventSource.addEventListener("portal.changed", function () {
        loadBranding();
      });
      SESSION_PUSH_EVENTS.forEach(function (name) {
        brandingEventSource.addEventListener(name, function (e) {
          handleSessionPush(e, name === "portal.coin.credit");
        });
      });
      brandingEventSource.onerror = function () {
        // EventSource reconnects on its own; polling remains the safety net.
      };
    } catch (e) {}
  }

  // A pushed payload is only applied when it belongs to this device — the
  // stream is shared by every connected customer.
  function handleSessionPush(event, isCoinCredit) {
    var payload;
    try {
      payload = JSON.parse(event && event.data ? event.data : "null");
    } catch (err) {
      payload = null;
    }
    if (!payload) return;

    var mine = String(payload.macAddress || payload.mac || "").toLowerCase();
    var self = String(getDeviceMAC() || "").toLowerCase();
    if (!self || !mine || mine !== self) return;

    var session = normalizeSession(payload);
    if (!session) return;
    noteApplianceSuccess();
    applyNormalizedSession(session, isCoinCredit);
    if (isCoinCredit) renderCoinModal();
  }

  function apiGet(path) {
    return fetch(API_BASE + path).then(function (r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    });
  }

  function apiPost(path, body) {
    return fetch(API_BASE + path, {
      method:  "POST",
      headers: { "Content-Type": "application/json" },
      body:    JSON.stringify(body || {})
    }).then(function (r) {
      return r.json().catch(function () { return null; }).then(function (json) {
        if (!r.ok) {
          var err = new Error(
            (json && (json.error || json.message)) || ("HTTP " + r.status));
          // Stable machine-readable reason from the firmware — prefer this over
          // matching the human-readable text.
          err.code = (json && json.code) || "";
          err.status = r.status;
          throw err;
        }
        return json;
      });
    });
  }

  function portalGet(path)       { return apiGet(path).then(unwrapPortalResponse); }
  function portalPost(path, body){ return apiPost(path, body).then(unwrapPortalResponse); }

  // ── Portal API ─────────────────────────────────────────────────────────────
  function fetchSession() {
    var mac = getDeviceMAC();
    var ip  = getDeviceIP();
    if (!mac) return Promise.reject(new Error("MAC address unavailable"));
    var gen = ++sessionSyncGen;
    var qs = "?mac=" + encodeURIComponent(mac) + "&ip=" + encodeURIComponent(ip);
    return portalGet("/session" + qs).then(function (raw) {
      return { gen: gen, session: normalizeSession(raw) };
    });
  }

  function applyFetchedSession(result, trustFully) {
    if (!result || !result.session) return null;
    // A newer GET has already started — drop this stale payload.
    if (result.gen !== sessionSyncGen) return null;
    applyNormalizedSession(result.session, trustFully);
    return result.session;
  }

  // Issue 2: background sync uses trustFully=false → drift prevention active
  function syncSessionFromServer() {
    return fetchSession().then(function (result) {
      noteApplianceSuccess();
      return applyFetchedSession(result, false);
    }, function (err) {
      noteApplianceFailure();
      throw err;
    });
  }

  function startCoinSessionAPI() {
    return portalPost("/start-coin-session", deviceParams()).then(function (raw) {
      var session = normalizeSession(raw);
      if (!session) {
        var err = new Error("Invalid coin session response from appliance");
        err.code = "INVALID_SESSION";
        throw err;
      }
      // Authoritative open — invalidate in-flight GET /session so a stale
      // pre-open snapshot cannot clear the new coin window.
      sessionSyncGen += 1;
      return session;
    });
  }
  function donePayingAPI()         { return portalPost("/done-paying",        deviceParams()).then(normalizeSession); }
  function pauseSessionAPI()       { return portalPost("/pause",              deviceParams()); }
  function resumeSessionAPI()      { return portalPost("/resume",             deviceParams()); }
  function cancelCoinModalAPI()    { return portalPost("/cancel-modal",       deviceParams()).then(normalizeSession); }
  function heartbeatAPI()          { return portalPost("/heartbeat",          deviceParams()); }
  function fetchRatesAPI()         { return portalGet("/rates").then(normalizeRatesPayload); }
  function redeemVoucherAPI(code) {
    var body = deviceParams();
    body.code = String(code || "").trim().toUpperCase();
    return portalPost("/voucher/redeem", body).then(normalizeSession);
  }
  function reconnectVoucherAPI() {
    return portalPost("/voucher/reconnect", deviceParams()).then(normalizeSession);
  }

  // Issue 4: terminate session API
  function terminateSessionAPI() {
    return portalPost("/terminate", deviceParams()).then(normalizeSession);
  }

  // ── audio ──────────────────────────────────────────────────────────────────
  function playMusic() {
    if (!dom.bgMusic) return;
    dom.bgMusic.currentTime = 0;
    dom.bgMusic.play().catch(function () {
      document.addEventListener("click", function tryPlay() {
        dom.bgMusic.play().catch(function () {});
        document.removeEventListener("click", tryPlay);
      }, { once: true });
    });
  }

  function stopMusic() {
    if (!dom.bgMusic) return;
    dom.bgMusic.pause();
    dom.bgMusic.currentTime = 0;
  }

  function playCoinSound() {
    if (!dom.coinSound) return;
    dom.coinSound.currentTime = 0;
    dom.coinSound.play().catch(function () {});
  }

  function playSuccessSound() {
    if (!dom.successSound) return;
    dom.successSound.currentTime = 0;
    dom.successSound.play().catch(function () {});
  }

  // ── modal helpers ──────────────────────────────────────────────────────────
  function openModal(modal) {
    if (!modal) return;
    modal.hidden = false;
    document.body.style.overflow = "hidden";
  }

  function closeModal(modal, skipApiCancel) {
    if (!modal) return;
    // Don't let a backdrop tap or Escape dismiss the dialog mid-request; the
    // customer must see whether their session actually ended.
    if (modal === dom.terminateModal && terminateInFlight) return;
    modal.hidden = true;
    if (!document.querySelector(".modal:not([hidden])")) {
      document.body.style.overflow = "";
    }
    if (modal === dom.coinModal) {
      coinModalVisible = false;
      stopCoinSessionUI();
      if (!skipApiCancel && !coinCancelInFlight && !donePayingInFlight) {
        requestCancelCoinModal();
      }
    }
  }

  function requestCancelCoinModal() {
    if (coinCancelInFlight) return;
    coinCancelInFlight = true;
    cancelCoinModalAPI()
      .catch(function () {})
      .finally(function () { coinCancelInFlight = false; });
  }

  function handleCoinTimeout() {
    if (coinTimeoutHandled || !state.coinSessionActive) return;
    coinTimeoutHandled = true;
    stopCoinSessionUI();
    closeModal(dom.coinModal, true);
    requestCancelCoinModal();
  }

  function startCoinSessionPoll() {
    clearInterval(coinPollTimer);
    coinPollTimer = setInterval(function () {
      syncSessionFromServer().catch(function () {});
    }, COIN_POLL_MS);
    syncSessionFromServer().catch(function () {});
  }

  function stopCoinSessionPoll() {
    clearInterval(coinPollTimer);
    coinPollTimer = null;
  }

  function startCoinSessionUI(serverCountdown, isRestore) {
    if (coinModalVisible) {
      state.coinCountdown = Number(serverCountdown) || state.coinCountdown || INSERT_TIMEOUT;
      anchorCoinWindow(state.coinCountdown);
      renderCoinModal();
      return;
    }

    state.coinSessionActive  = true;
    state.coinCountdown      = Number(serverCountdown) || INSERT_TIMEOUT;
    coinTimeoutHandled       = false;

    if (isRestore) {
      state.coinAmount = Number(state.insertedAmount) || state.coinAmount || 0;
      state.coinBaseCredits = Math.max(0, state.credits - state.coinAmount);
      lastWindowInserted = state.coinAmount;
    } else {
      state.coinBaseCredits    = state.credits;
      state.coinAmount         = Number(state.insertedAmount) || 0;
      state.coinMinutes        = 0;
      state.coinVoucherMinutes = 0;
      state.coinPulseCount     = 0;
      lastWindowInserted       = state.coinAmount;
    }

    coinModalVisible = true;
    anchorCoinWindow(state.coinCountdown);
    openModal(dom.coinModal);
    playMusic();
    renderCoinModal();
    startCoinSessionPoll();

    // Repaint only — the window deadline is the ESP32's coinWindowRemaining,
    // re-anchored on every payload, so the modal cannot outlive the firmware
    // window or close early.
    clearInterval(coinTimer);
    coinTimer = setInterval(function () {
      if (!state.coinSessionActive) return;
      if (derivedCoinSeconds() <= 0) {
        handleCoinTimeout();
        return;
      }
      renderCoinModal();
    }, 1000);
  }

  function restoreCoinSessionUI() {
    if (!state.coinSessionActive || coinModalVisible) return;
    startCoinSessionUI(state.coinCountdown, true);
  }

  function stopCoinSessionUI() {
    clearInterval(coinTimer);
    coinTimer = null;
    stopCoinSessionPoll();
    state.coinSessionActive  = false;
    state.coinCountdown      = INSERT_TIMEOUT;
    state.coinAmount         = 0;
    state.coinMinutes        = 0;
    state.coinVoucherMinutes = 0;
    state.coinPulseCount     = 0;
    state.coinBaseCredits    = 0;
    lastWindowInserted       = 0;
    stopMusic();
    saveStateCache();
  }

  function showPortalError(message) {
    if (dom.statusEl) {
      dom.statusEl.textContent = message;
      dom.statusEl.classList.add("disconnected");
    }
  }

  function showSessionNotice(message) {
    if (!dom.sessionNotice) return;
    dom.sessionNotice.textContent = message;
    dom.sessionNotice.hidden = false;
    sessionNoticeTicks = 5;
  }

  function hideSessionNotice() {
    sessionNoticeTicks = 0;
    if (!dom.sessionNotice) return;
    dom.sessionNotice.hidden = true;
    dom.sessionNotice.textContent = "";
  }

  function updateSessionNotice(previousSeconds, currentSeconds) {
    if (state.sessionState !== "active" || !state.connected || state.paused) {
      hideSessionNotice();
      return;
    }
    if (!warnedAt15Seconds && currentSeconds > 0 && currentSeconds <= 15 &&
        (previousSeconds > 15 || currentSeconds === 15)) {
      warnedAt15Seconds = true;
      showSessionNotice(
        "15 seconds remaining. Your Internet connection will end soon."
      );
    } else if (!warnedAt30Seconds && currentSeconds > 15 &&
               currentSeconds <= 30 &&
               (previousSeconds > 30 || currentSeconds === 30)) {
      warnedAt30Seconds = true;
      showSessionNotice(
        "30 seconds remaining. Insert more coins to continue your session."
      );
    }
    if (currentSeconds <= 0) hideSessionNotice();
  }

  // ── appliance reachability notice ──────────────────────────────────────────
  // Purely a visibility layer over the existing background request failures
  // (heartbeat / session sync / initial load). Never touches cached credits
  // or session state, never calls MikroTik login, and clears itself as soon
  // as the appliance answers again — safe to leave running indefinitely.
  var applianceFailureCount = 0;

  function showServiceNotice() {
    if (dom.serviceNotice) dom.serviceNotice.hidden = false;
  }

  function hideServiceNotice() {
    if (dom.serviceNotice) dom.serviceNotice.hidden = true;
  }

  function noteApplianceFailure() {
    applianceFailureCount += 1;
    if (applianceFailureCount >= SERVICE_UNAVAILABLE_THRESHOLD) {
      showServiceNotice();
    }
  }

  function noteApplianceSuccess() {
    applianceFailureCount = 0;
    hideServiceNotice();
  }

  // ── button handlers ────────────────────────────────────────────────────────
  function handleInsertCoin() {
    if (!getDeviceMAC()) { showPortalError("Device MAC unavailable"); return; }
    if (dom.insertCoinBtn && dom.insertCoinBtn.disabled) return;
    if (dom.insertCoinBtn) dom.insertCoinBtn.disabled = true;

    startCoinSessionAPI()
      .then(function (session) {
        noteApplianceSuccess();
        if (!session) throw new Error("Invalid session response");
        applyNormalizedSession(session, true);
        startCoinSessionUI(session.coinCountdown, false);
      })
      .catch(function (err) {
        var code = (err && err.code) || "";
        var msg = (err && err.message) ? err.message : "Could not start a coin session.";
        // Appliance answered with a business error — do not treat as outage.
        if (code === "COIN_DISABLED" || code === "SESSION_ERROR" ||
            code === "MISSING_MAC" || code === "INVALID_SESSION" ||
            code === "VOUCHER_SESSION") {
          noteApplianceSuccess();
        } else {
          noteApplianceFailure();
          showServiceNotice();
        }
        if (code && msg.indexOf(code) === -1) {
          msg = msg + " (" + code + ")";
        }
        showPortalError(msg);
        showSessionNotice(msg);
      })
      .finally(function () {
        if (dom.insertCoinBtn) dom.insertCoinBtn.disabled = false;
      });
  }

  // State-driven wait. Firmware activating must never become a browser
  // timeout failure. Only activation_error is a terminal failure.
  function waitForActivation() {
    var lastHttpAt = 0;
    var HTTP_FALLBACK_MS = 2000;
    var LOCAL_TICK_MS = 250;
    var connectingNoticeAt = 0;

    function settled() {
      if (state.sessionState === "active" && state.connected) {
        return { done: true };
      }
      if (state.sessionState === "activation_error") {
        throw new Error(state.activationErrorReason ||
          "Activation failed — purchased time preserved");
      }
      return null;
    }

    function poll() {
      if (settled()) {
        if (window.console) {
          console.log("[activate-latency] T12_connected t=" + Date.now());
        }
        return Promise.resolve();
      }
      if (state.sessionState === "activating" ||
          (state.secondsLeft > 0 && !state.connected &&
           state.sessionState !== "activation_error")) {
        if (Date.now() - connectingNoticeAt >= 15000) {
          connectingNoticeAt = Date.now();
          if (dom.statusEl) {
            dom.statusEl.textContent = "Still connecting to the router…";
            dom.statusEl.classList.remove("disconnected");
          }
        }
      }
      var now = Date.now();
      var needHttp = lastHttpAt === 0 || (now - lastHttpAt) >= HTTP_FALLBACK_MS;
      function tick() {
        return new Promise(function (resolve) {
          setTimeout(resolve, LOCAL_TICK_MS);
        }).then(poll);
      }
      if (!needHttp) return tick();
      lastHttpAt = now;
      return fetchSession().then(function (result) {
        applyFetchedSession(result, false);
        if (settled()) {
          if (window.console) {
            console.log("[activate-latency] T12_connected t=" + Date.now());
          }
          return;
        }
        return tick();
      });
    }
    if (window.console) {
      console.log("[activate-latency] T11_wait_start t=" + Date.now());
    }
    return poll();
  }

  function handleDonePaying() {
    if (donePayingInFlight) return;
    donePayingInFlight = true;
    var activationErrorMessage = "";

    stopCoinSessionUI();
    closeModal(dom.coinModal, true);
    if (dom.statusEl) {
      dom.statusEl.textContent = "Activating…";
      dom.statusEl.classList.remove("disconnected");
    }

    donePayingAPI()
      .then(function (session) {
        noteApplianceSuccess();
        sessionSyncGen += 1;
        if (session) applyNormalizedSession(session, true);
        var st = (session && session.sessionState) || "";
        if (st === "activating" || (session && session.secondsLeft > 0 && !session.connected)) {
          return waitForActivation();
        }
      })
      .then(function () {
        if (state.sessionState === "active" && state.connected &&
            state.secondsLeft > 0) {
          playSuccessSound();
        }
        startMainTimer();
        renderStatus();
        renderInsertBtn();
        renderTerminateBtn();
      })
      .catch(function (err) {
        activationErrorMessage =
          err && err.message ? err.message : "Activation failed";
        var code = (err && err.code) || "";
        if (code === "ACTIVATION_QUEUE_FULL" || code === "NO_CREDITS" ||
            code === "NO_MINUTES" || code === "VOUCHER_SESSION") {
          noteApplianceSuccess();
        } else if (state.sessionState === "activation_error") {
          noteApplianceSuccess();
        } else if (state.sessionState === "activating") {
          noteApplianceSuccess();
          activationErrorMessage = "";
        } else {
          noteApplianceFailure();
          showServiceNotice();
        }
        return syncSessionFromServer().catch(function () {});
      })
      .finally(function () {
        donePayingInFlight = false;
        render();
        if (activationErrorMessage &&
            state.sessionState === "activation_error") {
          showPortalError(activationErrorMessage);
        }
      });
  }

  function handleTogglePause() {
    if (!dom.pauseButton || dom.pauseButton.disabled) return;
    dom.pauseButton.disabled = true;

    // canResume covers paused sessions and activation_error retry (firmware
    // resume() re-queues hotspot authorization without consuming credits).
    var resuming = state.canResume && !state.canPause;
    var apiCall = resuming ? resumeSessionAPI : pauseSessionAPI;
    if (resuming && state.sessionState === "activation_error" && dom.statusEl) {
      dom.statusEl.textContent = "Activating…";
      dom.statusEl.classList.remove("disconnected");
    }
    apiCall()
      .then(function () {
        noteApplianceSuccess();
        // Pause/resume authorization happens on the router worker; re-read the
        // session so the button and countdown reflect the settled state rather
        // than an optimistic guess.
        return syncSessionFromServer();
      })
      .then(function () {
        if (resuming &&
            (state.sessionState === "activating" ||
             (state.secondsLeft > 0 && !state.connected))) {
          return waitForActivation();
        }
      })
      .then(function () {
        if (resuming && state.secondsLeft > 0 && state.connected) {
          playSuccessSound();
          startMainTimer();
        }
      })
      .catch(function (err) {
        var code = (err && err.code) || "";
        if (code === "PAUSE_LIMIT_REACHED") {
          // A refused pause is a rule, not an outage — don't scare the customer
          // with the service-unavailable banner.
          showSessionNotice(
            "You have used all " + (state.pauseLimit || 3) +
            " pauses for this session."
          );
          return syncSessionFromServer().catch(function () {});
        }
        noteApplianceFailure();
        var msg = err && err.message ? err.message : "";
        if (resuming && msg) {
          showPortalError(msg);
        } else {
          showSessionNotice(resuming
            ? "Could not resume yet. Retrying automatically…"
            : "Could not pause right now. Please try again.");
        }
        return syncSessionFromServer().catch(function () {});
      })
      .finally(function () {
        if (dom.pauseButton) dom.pauseButton.disabled = false;
        render();
      });
  }

  function handleViewRates() {
    fetchRatesAPI()
      .then(function (viewModel) {
        renderRatesModal(viewModel);
        openModal(dom.ratesModal);
      })
      .catch(function (err) {
        if (dom.ratesList) {
          var msg = (err && err.message) ? err.message : "Unable to load rates from appliance";
          dom.ratesList.innerHTML =
            '<p class="rates-loading">' + escapeHtml(msg) + '</p>';
        }
        openModal(dom.ratesModal);
      });
  }

  // ── terminate session ──────────────────────────────────────────────────────
  var terminateInFlight = false;

  function setTerminateStatus(message, isError) {
    if (!dom.terminateStatus) return;
    dom.terminateStatus.textContent = message || "";
    dom.terminateStatus.hidden = !message;
    dom.terminateStatus.classList.toggle("error", Boolean(isError));
  }

  function setTerminateBusy(busy) {
    if (dom.confirmTerminateBtn) {
      dom.confirmTerminateBtn.disabled = busy;
      dom.confirmTerminateBtn.textContent = busy ? "Terminating…" : "Terminate now";
    }
    if (dom.cancelTerminateBtn) dom.cancelTerminateBtn.disabled = busy;
  }

  function openTerminateModal() {
    if (!state.canTerminate) return;
    setTerminateStatus("", false);
    setTerminateBusy(false);
    renderTerminateModal();
    openModal(dom.terminateModal);
  }

  // Shows exactly what the customer forfeits, so the confirmation is informed
  // rather than a bare yes/no.
  function renderTerminateModal() {
    if (dom.terminateRemaining) {
      dom.terminateRemaining.textContent = formatTime(displaySeconds());
    }
    if (dom.terminateCredits) {
      dom.terminateCredits.textContent = "\u20B1" + (state.credits || 0).toFixed(2);
    }
  }

  function handleTerminateConfirm() {
    if (terminateInFlight) return;
    terminateInFlight = true;
    setTerminateBusy(true);
    setTerminateStatus("Ending your session…", false);

    terminateSessionAPI()
      .then(function (session) {
        noteApplianceSuccess();
        // The firmware reply is the post-terminate session; apply it verbatim
        // instead of guessing a cleared state locally.
        if (session) applyNormalizedSession(session, true);
        // Release the dismissal guard before closing — it exists to block the
        // customer's taps during the request, not this success path.
        terminateInFlight = false;
        closeModal(dom.terminateModal, true);
        setTerminateStatus("", false);
        render();
        // Confirm the router-side deauthorize landed rather than trusting the
        // acknowledgement alone.
        return syncSessionFromServer().catch(function () {});
      })
      .catch(function (err) {
        noteApplianceFailure();
        setTerminateStatus(
          (err && err.message)
            ? err.message + " — your session was not changed."
            : "Could not end the session. Please try again.",
          true
        );
      })
      .finally(function () {
        terminateInFlight = false;
        setTerminateBusy(false);
      });
  }

  function setVoucherBusy(busy, message, isError) {
    if (dom.voucherSubmitBtn) dom.voucherSubmitBtn.disabled = Boolean(busy);
    if (dom.voucherCode) dom.voucherCode.disabled = Boolean(busy);
    if (dom.voucherCard) dom.voucherCard.classList.toggle("is-busy", Boolean(busy));
    if (dom.voucherHelp) {
      dom.voucherHelp.textContent =
        message || (busy ? "Validating voucher…" : "Voucher is case-insensitive");
      dom.voucherHelp.classList.toggle("error", Boolean(isError));
      dom.voucherHelp.classList.toggle("success", Boolean(message) && !isError);
    }
  }

  function handleVoucherSubmit(event) {
    event.preventDefault();
    if (!dom.voucherCode || !dom.voucherSubmitBtn ||
        dom.voucherSubmitBtn.disabled) return;
    var code = String(dom.voucherCode.value || "").trim().toUpperCase();
    if (!code) {
      setVoucherBusy(false, "Enter a voucher code.", true);
      return;
    }

    setVoucherBusy(true, "Validating voucher…", false);
    redeemVoucherAPI(code)
      .then(function (session) {
        if (session) applyNormalizedSession(session, true);
        setVoucherBusy(true, "Voucher accepted. Activating Internet…", false);
        return waitForActivation();
      })
      .then(function () {
        noteApplianceSuccess();
        playSuccessSound();
        setVoucherBusy(false, "Voucher active on this device.", false);
      })
      .catch(function (err) {
        noteApplianceFailure();
        setVoucherBusy(false, err && err.message ? err.message :
          "Voucher activation failed.", true);
      });
  }

  function maybeReconnectVoucher(session) {
    if (!session || session.source !== "voucher" || !session.canReconnect ||
        session.sessionState === "expired" || session.voucherStatus === "expired" ||
        session.sessionState === "expiring") {
      return Promise.resolve(session);
    }
    setVoucherBusy(true, "Restoring voucher Internet access…", false);
    return reconnectVoucherAPI()
      .then(function (updated) {
        if (updated) applyNormalizedSession(updated, true);
        return waitForActivation();
      })
      .then(function (active) {
        setVoucherBusy(false, "Voucher active on this device.", false);
        return active;
      })
      .catch(function (err) {
        setVoucherBusy(false, err && err.message ? err.message :
          "Unable to restore voucher access.", true);
        return session;
      });
  }

  // ── event bindings ─────────────────────────────────────────────────────────
  function bindEvents() {
    if (dom.insertCoinBtn) dom.insertCoinBtn.addEventListener("click", handleInsertCoin);
    if (dom.pauseButton)   dom.pauseButton.addEventListener("click", handleTogglePause);
    if (dom.viewRatesBtn)  dom.viewRatesBtn.addEventListener("click", handleViewRates);
    if (dom.donePayingBtn) dom.donePayingBtn.addEventListener("click", handleDonePaying);
    if (dom.voucherForm) dom.voucherForm.addEventListener("submit", handleVoucherSubmit);

    if (dom.terminateBtn) {
      dom.terminateBtn.addEventListener("click", openTerminateModal);
    }
    if (dom.confirmTerminateBtn) {
      dom.confirmTerminateBtn.addEventListener("click", handleTerminateConfirm);
    }
    if (dom.cancelTerminateBtn) {
      dom.cancelTerminateBtn.addEventListener("click", function () {
        closeModal(dom.terminateModal, true);
      });
    }

    document.addEventListener("click", function (e) {
      if (e.target.closest("[data-close-modal]")) {
        closeModal(e.target.closest(".modal"));
      } else if (e.target.classList && e.target.classList.contains("modal")) {
        closeModal(e.target);
      }
    });

    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") {
        document.querySelectorAll(".modal").forEach(function (m) { closeModal(m); });
      }
    });
  }

  // Repaint loop only. It owns no value: every tick re-reads the deadline the
  // firmware established, so the display can never disagree with the ESP32 by
  // more than the transport delay of the last payload.
  var lastPaintedSeconds = null;

  function startMainTimer() {
    clearInterval(mainTimer);
    mainTimer = setInterval(function () {
      if (sessionNoticeTicks > 0) {
        sessionNoticeTicks -= 1;
        if (sessionNoticeTicks === 0) hideSessionNotice();
      }
      var shown = displaySeconds();
      if (lastPaintedSeconds !== null && shown !== lastPaintedSeconds) {
        updateSessionNotice(lastPaintedSeconds, shown);
      }
      lastPaintedSeconds = shown;
      renderStatus();
      renderMainTimer();
      renderInsertBtn();
      renderCoinModal();
      if (dom.terminateModal && !dom.terminateModal.hidden) renderTerminateModal();
    }, 1000);
  }

  function heartbeat() {
    heartbeatAPI()
      .then(function () {
        // syncSessionFromServer() already tracks its own success/failure —
        // swallow here so a sync failure isn't double-counted below.
        return syncSessionFromServer().catch(function () {});
      })
      .catch(function () { noteApplianceFailure(); });
  }

  function startHeartbeat() {
    clearInterval(heartbeatTimer);
    heartbeatTimer = setInterval(heartbeat, HEARTBEAT_MS);
  }

  // ── render ─────────────────────────────────────────────────────────────────
  function render() {
    renderStatus();
    renderCredits();
    renderMainTimer();
    renderInsertBtn();
    renderPause();
    renderTerminateBtn();
    renderVoucherControls();
    renderCoinModal();
    if (dom.terminateModal && !dom.terminateModal.hidden) renderTerminateModal();
    renderDate();
  }

  function renderStatus() {
    if (!dom.statusEl) return;
    var st = state.sessionState || "";
    var label = "Disconnected";
    var disconnected = true;
    if (st === "activating") {
      label = "Activating…";
      disconnected = false;
    } else if (st === "waiting_coin") {
      label = "Waiting for Payment";
    } else if (st === "activation_error") {
      // Exact firmware reason is the status of record — never hide it behind a
      // generic "Activation failed" label (title-only was invisible on mobile).
      label = state.activationErrorReason
        ? state.activationErrorReason
        : "Activation failed — purchased time preserved";
    } else if (st === "expiring") {
      label = "Disconnecting…";
    } else if (st === "paused" || state.paused) {
      label = "Paused";
      disconnected = true;
    } else if (state.connected && state.secondsLeft > 0 && st === "active") {
      label = "Connected";
      disconnected = false;
    }
    dom.statusEl.textContent = label;
    dom.statusEl.classList.toggle("disconnected", disconnected);
    dom.statusEl.title = st === "activation_error" && state.activationErrorReason
      ? state.activationErrorReason
      : "";
  }

  function renderMainTimer() {
    if (dom.mainTimerEl) dom.mainTimerEl.textContent = formatTime(displaySeconds());
  }

  function renderCredits() {
    if (dom.creditsEl) {
      dom.creditsEl.textContent = "\u20B1" + (state.credits || 0).toFixed(2);
    }
  }

  // Issue 4: rename INSERT COIN → ADD ADDITIONAL TIME when session is active
  function renderInsertBtn() {
    if (dom.insertCoinBtn) {
      dom.insertCoinBtn.hidden = !state.canInsertCoin;
    }
    if (dom.insertCoinLabel) {
      dom.insertCoinLabel.textContent =
        state.secondsLeft > 0 ? "ADD ADDITIONAL TIME" : "INSERT COIN";
    }
  }

  function renderPause() {
    var remaining = state.pausesRemaining;
    if (dom.pauseButtonText) {
      var label;
      if (state.sessionState === "activation_error" && state.canResume) {
        label = "RETRY INTERNET";
      } else {
        label = state.paused ? "RESUME" : "PAUSE";
        if (!state.paused && remaining !== null && remaining !== undefined) {
          label += " (" + remaining + " left)";
        }
      }
      dom.pauseButtonText.textContent = label;
    }
    if (dom.pauseButton) {
      dom.pauseButton.hidden = !state.canPause && !state.canResume;
      dom.pauseButton.setAttribute("aria-pressed", state.paused ? "true" : "false");
      if (state.sessionState === "activation_error") {
        dom.pauseButton.title = state.activationErrorReason ||
          "Retry hotspot authorization — purchased time is preserved";
      } else {
        dom.pauseButton.title = !state.paused && remaining === 0
          ? "Pause limit reached for this session"
          : "";
      }
    }
  }

  // Issue 4: show/hide terminate button only when session is active
  function renderTerminateBtn() {
    if (!dom.terminateBtn) return;
    var hasSession = state.secondsLeft > 0 || state.paused;
    dom.terminateBtn.hidden = !hasSession || !state.canTerminate;
  }

  function renderVoucherControls() {
    if (!dom.voucherCard) return;
    var activeVoucher = state.source === "voucher" &&
      state.sessionState !== "expired" && state.secondsLeft > 0;
    dom.voucherCard.hidden = activeVoucher;
  }

  function renderCoinModal() {
    var secs      = state.coinSessionActive ? derivedCoinSeconds() : INSERT_TIMEOUT;
    // Always use coinAmount — resets to 0 when modal opens, grows with new pulses only.
    var coinAmt   = state.coinAmount || 0;
    // Purchased time is ESP32/PromoManager authoritative (purchasedMinutes).
    var purchased = Number(state.purchasedMinutes) || 0;
    if (state.coinSessionActive && coinAmt > 0 && purchased <= 0) {
      // Keep last known server value; never invent a hardcoded minutes fallback in the browser.
      purchased = Number(state.coinVoucherMinutes) || 0;
    }
    state.coinVoucherMinutes = purchased;

    if (dom.coinCountdownEl)  dom.coinCountdownEl.textContent  = String(secs);
    if (dom.coinProgressBar) {
      dom.coinProgressBar.style.width = (secs / INSERT_TIMEOUT * 100) + "%";
    }
    if (dom.coinTimeEl) {
      dom.coinTimeEl.textContent = purchased > 0 ? purchased + "m" : "0m";
    }
    // #coinVoucherTime removed from customer UI (was duplicate of Time).
    if (dom.insertedAmountEl) {
      dom.insertedAmountEl.textContent = coinAmt.toFixed(2);
    }
    if (dom.coinNoteEl) {
      dom.coinNoteEl.textContent = state.coinSessionActive
        ? "Waiting for coin pulses from this device."
        : "Coin insert session is idle. Credits stay on this device.";
    }
  }

  // Issue 5: richer rate card rendering with speed profile and device limit
  function renderRatesModal(viewModel) {
    if (!dom.ratesList || !viewModel || !Array.isArray(viewModel.rates)) return;
    dom.ratesList.innerHTML = viewModel.rates.map(function (r) {
      var extras = "";
      if (r.speedProfile) {
        extras += '<span class="rate-meta">&#x1F4F6; ' + escapeHtml(r.speedProfile) + '</span>';
      }
      if (r.deviceLimit > 0) {
        extras += '<span class="rate-meta">&#x1F4BB; Max ' + r.deviceLimit + ' device' +
                  (r.deviceLimit > 1 ? "s" : "") + '</span>';
      }
      return '<div class="rate-card">' +
               '<span>&#8369;' + r.peso + '</span>' +
               '<strong>' + r.minutes + ' mins</strong>' +
               (extras ? '<div class="rate-extras">' + extras + '</div>' : '') +
             '</div>';
    }).join("");
  }

  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function renderDate() {
    if (dom.currentDateEl) {
      dom.currentDateEl.textContent = new Date().toLocaleString("en-PH", {
        weekday: "short", month: "short", day: "numeric",
        hour: "numeric", minute: "2-digit"
      });
    }
  }

  function pad(n) { return String(n).padStart(2, "0"); }

  function formatTime(sec) {
    var s = Math.max(0, (sec | 0));
    return pad(Math.floor(s / 3600)) + ":" +
           pad(Math.floor((s % 3600) / 60)) + ":" +
           pad(s % 60);
  }

  // ── init ───────────────────────────────────────────────────────────────────
  function init() {
    cacheDom();

    storageKey = STORAGE_PREFIX + ":" + getDeviceKey();
    state = loadCachedState();
    // Show the cached value frozen until the firmware speaks; it is a
    // placeholder, not a running clock.
    anchorSession(state.secondsLeft, false);
    render();

    // Issue 2: initial fetch uses trustFully=true (fresh load)
    fetchSession()
      .then(function (result) {
        noteApplianceSuccess();
        var session = applyFetchedSession(result, true);
        if (session) {
          if (session.coinSessionActive) restoreCoinSessionUI();
          return maybeReconnectVoucher(session).then(function () {
            startMainTimer();
          });
        }
        startMainTimer();
      })
      .catch(function () {
        noteApplianceFailure();
        startMainTimer();
      });

    bindEvents();
    loadBranding();
    connectPortalEvents();
    setInterval(renderDate, 1000);
    startHeartbeat();

    window.RenzFiPortalCoinDetected = function (amount) {
      syncSessionFromServer().catch(function () {
        var peso = Math.max(1, Number(amount) || 1);
        state.credits = (state.credits || 0) + peso;
        saveStateCache();
        renderCoinModal();
        renderCredits();
      });
    };

    document.addEventListener("renzfi:coin", function (e) {
      window.RenzFiPortalCoinDetected(e.detail && e.detail.amount);
    });

    window.RenzFiPortalReady = true;
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
}());
