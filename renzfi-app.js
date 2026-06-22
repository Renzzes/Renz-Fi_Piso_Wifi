(function () {
  "use strict";

  // ── configuration ──────────────────────────────────────────────────────────
  var API_BASE          = "http://10.40.0.2/api/portal";
  var STORAGE_PREFIX    = "renzFiPortalState";
  var INSERT_TIMEOUT    = 60;
  var HEARTBEAT_MS      = 10000;
  var COIN_POLL_MS      = 2000;
  var MINUTES_PER_PESO  = 5;

  // ── timers ─────────────────────────────────────────────────────────────────
  var mainTimer      = null;
  var coinTimer      = null;
  var coinPollTimer  = null;
  var heartbeatTimer = null;

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
  // Firmware wraps payloads: { success, data, message }
  function unwrapPortalResponse(json) {
    if (!json || json.success !== true) {
      var msg = (json && json.error) ? json.error : "API request failed";
      throw new Error(msg);
    }
    return json.data;
  }

  // Map firmware session fields → portal UI state (no firmware changes).
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

    return {
      secondsLeft:      Number(raw.secondsLeft      || raw.seconds_left)      || 0,
      credits:          Number(raw.credits)                                    || 0,
      paused:           Boolean(raw.paused),
      coinSessionActive: Boolean(coinActive),
      coinCountdown:    Number(coinCountdown) || INSERT_TIMEOUT,
      insertedAmount:   Number(raw.insertedAmount   || raw.inserted_amount)   || 0
    };
  }

  // Map firmware promo array (coin/minutes) → portal rates view model.
  function normalizeRatesPayload(rawData) {
    var promos = Array.isArray(rawData) ? rawData : (rawData && rawData.rates);
    if (!Array.isArray(promos)) return null;

    var rates = promos.map(function (p) {
      return {
        peso:    Number(p.coin != null ? p.coin : p.peso) || 0,
        minutes: Number(p.minutes) || 0
      };
    }).filter(function (r) {
      return r.peso > 0 && r.minutes > 0;
    });

    rates.sort(function (a, b) { return a.peso - b.peso; });

    var minutesPerPeso = MINUTES_PER_PESO;
    for (var i = 0; i < rates.length; i++) {
      if (rates[i].peso === 1) {
        minutesPerPeso = rates[i].minutes;
        break;
      }
    }
    if (minutesPerPeso === MINUTES_PER_PESO && rates.length > 0) {
      minutesPerPeso = rates[0].minutes / rates[0].peso;
    }

    return { rates: rates, minutesPerPeso: minutesPerPeso };
  }

  // ── localStorage (UI cache only) ───────────────────────────────────────────
  var storageKey = STORAGE_PREFIX + ":device";

  function defaultState() {
    return {
      credits:           0,
      secondsLeft:       0,
      paused:            false,
      coinCountdown:     INSERT_TIMEOUT,
      coinSessionActive: false,
      insertedAmount:    0
    };
  }

  function loadCachedState() {
    try {
      var saved = JSON.parse(localStorage.getItem(storageKey) || "{}");
      return {
        credits:           Number(saved.credits)           || 0,
        secondsLeft:       Number(saved.secondsLeft)       || 0,
        paused:            Boolean(saved.paused),
        coinCountdown:     Number(saved.coinCountdown)     || INSERT_TIMEOUT,
        coinSessionActive: Boolean(saved.coinSessionActive),
        insertedAmount:    Number(saved.insertedAmount)    || 0
      };
    } catch (e) {
      return defaultState();
    }
  }

  function saveStateCache() {
    localStorage.setItem(storageKey, JSON.stringify(state));
  }

  var state = defaultState();

  function applyNormalizedSession(session) {
    if (!session) return;
    state.secondsLeft       = session.secondsLeft;
    state.credits           = session.credits;
    state.paused            = session.paused;
    state.coinSessionActive = session.coinSessionActive;
    state.coinCountdown     = session.coinCountdown;
    state.insertedAmount    = session.insertedAmount;
    saveStateCache();
    render();
  }

  function applySessionData(raw) {
    applyNormalizedSession(normalizeSession(raw));
  }

  // ── DOM refs ───────────────────────────────────────────────────────────────
  var dom = {};

  function cacheDom() {
    dom.insertCoinBtn    = document.getElementById("insertCoinBtn") ||
                           document.getElementById("insertCoinButton");
    dom.insertCoinLabel  = dom.insertCoinBtn &&
                           dom.insertCoinBtn.querySelector(".action-label");
    dom.pauseButton      = document.getElementById("pauseButton");
    dom.pauseButtonText  = document.getElementById("pauseButtonText");
    dom.viewRatesBtn     = document.getElementById("viewRatesBtn") ||
                           document.getElementById("viewRatesButton");
    dom.coinModal        = document.getElementById("coinModal");
    dom.ratesModal       = document.getElementById("ratesModal");
    dom.mainTimerEl      = document.getElementById("mainTimer");
    dom.creditsEl        = document.getElementById("credits");
    dom.currentDateEl    = document.getElementById("currentDate");
    dom.statusEl         = document.getElementById("connectionStatus");
    dom.coinCountdownEl  = document.getElementById("coinCountdown");
    dom.coinProgressBar  = document.getElementById("coinProgressBar");
    dom.coinTimeEl       = document.getElementById("coinTime");
    dom.coinVoucherTime  = document.getElementById("coinVoucherTime");
    dom.insertedAmountEl = document.getElementById("insertedAmount");
    dom.coinNoteEl       = document.getElementById("coinNote");
    dom.donePayingBtn    = document.getElementById("donePayingBtn");
    dom.bgMusic          = document.getElementById("bgMusic");
    dom.ratesList        = document.querySelector("#ratesModal .rates-list");
  }

  // ── HTTP helpers ───────────────────────────────────────────────────────────
  var BRANDING_BASE = API_BASE.replace(/\/api\/portal\/?$/, "");
  var brandingRevision = 0;
  var brandingEventSource = null;

  function applyBranding(data) {
    if (!data) return;

    var bannerEl = document.getElementById("portalBanner");
    if (bannerEl) {
      if (data.hasCustomBanner && data.bannerUrl) {
        bannerEl.src = data.bannerUrl;
      } else {
        bannerEl.src =
          bannerEl.getAttribute("data-default-src") || "Default-Banner.png";
      }
    }

    if (dom.bgMusic) {
      var musicSrc =
        data.hasCustomMusic && data.musicUrl
          ? data.musicUrl
          : "bg_music.mp3";
      var current = dom.bgMusic.getAttribute("src") || dom.bgMusic.src || "";
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
      .then(function (json) {
        applyBranding(unwrapPortalResponse(json));
      })
      .catch(function () {
        /* keep MikroTik-local defaults when ESP is unreachable */
      });
  }

  function connectBrandingEvents() {
    if (brandingEventSource) {
      brandingEventSource.close();
      brandingEventSource = null;
    }
    try {
      brandingEventSource = new EventSource(BRANDING_BASE + "/api/events");
      brandingEventSource.addEventListener("portal.changed", function () {
        loadBranding();
      });
    } catch (e) {
      /* EventSource unavailable */
    }
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
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    });
  }

  function portalGet(path) {
    return apiGet(path).then(unwrapPortalResponse);
  }

  function portalPost(path, body) {
    return apiPost(path, body).then(unwrapPortalResponse);
  }

  // ── Portal API (returns normalized session / rates view models) ───────────
  function fetchSession() {
    var mac = getDeviceMAC();
    var ip  = getDeviceIP();
    if (!mac) return Promise.reject(new Error("MAC address unavailable"));
    var qs = "?mac=" + encodeURIComponent(mac) + "&ip=" + encodeURIComponent(ip);
    return portalGet("/session" + qs).then(normalizeSession);
  }

  function syncSessionFromServer() {
    return fetchSession().then(function (session) {
      if (session) applyNormalizedSession(session);
      return session;
    });
  }

  function startCoinSessionAPI() {
    return portalPost("/start-coin-session", deviceParams()).then(normalizeSession);
  }

  function donePayingAPI() {
    return portalPost("/done-paying", deviceParams()).then(normalizeSession);
  }

  function pauseSessionAPI() {
    return portalPost("/pause", deviceParams());
  }

  function resumeSessionAPI() {
    return portalPost("/resume", deviceParams());
  }

  function cancelCoinModalAPI() {
    return portalPost("/cancel-modal", deviceParams()).then(normalizeSession);
  }

  function heartbeatAPI() {
    return portalPost("/heartbeat", deviceParams());
  }

  function fetchRatesAPI() {
    return portalGet("/rates").then(normalizeRatesPayload);
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

  // ── modal helpers ──────────────────────────────────────────────────────────
  function openModal(modal) {
    if (!modal) return;
    modal.hidden = false;
    document.body.style.overflow = "hidden";
  }

  function closeModal(modal, skipApiCancel) {
    if (!modal) return;
    modal.hidden = true;
    if (!document.querySelector(".modal:not([hidden])")) {
      document.body.style.overflow = "";
    }
    if (modal === dom.coinModal) {
      stopCoinSessionUI();
      if (!skipApiCancel) cancelCoinModalAPI().catch(function () {});
    }
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

  // Local smooth countdown between server polls; server values win on each poll.
  function startCoinSessionUI(serverCountdown) {
    state.coinSessionActive = true;
    state.coinCountdown     = Number(serverCountdown) || INSERT_TIMEOUT;
    openModal(dom.coinModal);
    playMusic();
    renderCoinModal();
    startCoinSessionPoll();

    clearInterval(coinTimer);
    coinTimer = setInterval(function () {
      if (state.coinCountdown > 0) {
        state.coinCountdown = Math.max(0, state.coinCountdown - 1);
        renderCoinModal();
      }
      if (state.coinCountdown === 0) {
        stopCoinSessionUI();
        closeModal(dom.coinModal, true);
        cancelCoinModalAPI().catch(function () {});
      }
    }, 1000);
  }

  function stopCoinSessionUI() {
    clearInterval(coinTimer);
    coinTimer = null;
    stopCoinSessionPoll();
    state.coinSessionActive = false;
    state.coinCountdown     = INSERT_TIMEOUT;
    stopMusic();
    saveStateCache();
  }

  function showPortalError(message) {
    if (dom.statusEl) {
      dom.statusEl.textContent = message;
      dom.statusEl.classList.add("disconnected");
    }
  }

  // ── button handlers ────────────────────────────────────────────────────────
  function handleInsertCoin() {
    if (!getDeviceMAC()) {
      showPortalError("Device MAC unavailable");
      return;
    }
    if (dom.insertCoinBtn && dom.insertCoinBtn.disabled) return;

    if (dom.insertCoinBtn) dom.insertCoinBtn.disabled = true;
    startCoinSessionAPI()
      .then(function (session) {
        if (!session) throw new Error("Invalid session response");
        applyNormalizedSession(session);
        startCoinSessionUI(session.coinCountdown);
      })
      .catch(function () {
        showPortalError("Could not start coin session");
        if (dom.coinNoteEl) {
          dom.coinNoteEl.textContent =
            "Unable to reach the payment server. Coin insertion is disabled until connected.";
        }
      })
      .finally(function () {
        if (dom.insertCoinBtn) dom.insertCoinBtn.disabled = false;
      });
  }

  function handleDonePaying() {
    donePayingAPI()
      .then(function (session) {
        stopCoinSessionUI();
        closeModal(dom.coinModal, true);
        if (session) applyNormalizedSession(session);
        startMainTimer();
        renderStatus();
      })
      .catch(function () {
        if (dom.coinNoteEl) {
          dom.coinNoteEl.textContent = "Could not reach the server. Please try again.";
        }
      });
  }

  function handleTogglePause() {
    var apiCall = state.paused ? resumeSessionAPI : pauseSessionAPI;
    apiCall()
      .then(function () { return syncSessionFromServer(); })
      .catch(function () {
        state.paused = !state.paused;
        saveStateCache();
        renderPause();
      });
  }

  function handleViewRates() {
    fetchRatesAPI()
      .then(function (viewModel) {
        renderRatesModal(viewModel);
        openModal(dom.ratesModal);
      })
      .catch(function () {
        openModal(dom.ratesModal);
      });
  }

  // ── event bindings ─────────────────────────────────────────────────────────
  function bindEvents() {
    if (dom.insertCoinBtn) dom.insertCoinBtn.addEventListener("click", handleInsertCoin);
    if (dom.pauseButton)   dom.pauseButton.addEventListener("click", handleTogglePause);
    if (dom.viewRatesBtn)  dom.viewRatesBtn.addEventListener("click", handleViewRates);
    if (dom.donePayingBtn) dom.donePayingBtn.addEventListener("click", handleDonePaying);

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

  function startMainTimer() {
    clearInterval(mainTimer);
    mainTimer = setInterval(function () {
      if (state.secondsLeft > 0 && !state.paused) {
        state.secondsLeft = Math.max(0, state.secondsLeft - 1);
        saveStateCache();
        renderStatus();
        renderMainTimer();
        renderInsertBtn();
      }
    }, 1000);
  }

  // Option 2: heartbeat keep-alive, then full session sync via GET /session.
  function heartbeat() {
    heartbeatAPI()
      .then(function () { return syncSessionFromServer(); })
      .catch(function () { /* keep last known state */ });
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
    renderCoinModal();
    renderDate();
  }

  function renderStatus() {
    if (!dom.statusEl) return;
    var active = state.secondsLeft > 0;
    dom.statusEl.textContent = active ? "Connected" : "Disconnected";
    dom.statusEl.classList.toggle("disconnected", !active);
  }

  function renderMainTimer() {
    if (dom.mainTimerEl) dom.mainTimerEl.textContent = formatTime(state.secondsLeft);
  }

  function renderCredits() {
    if (dom.creditsEl) {
      dom.creditsEl.textContent = "\u20B1" + (state.credits || 0).toFixed(2);
    }
  }

  function renderInsertBtn() {
    if (dom.insertCoinLabel) {
      dom.insertCoinLabel.textContent =
        state.secondsLeft > 0 ? "ADD ADDITIONAL TIME" : "INSERT COIN";
    }
  }

  function renderPause() {
    if (dom.pauseButtonText) {
      dom.pauseButtonText.textContent = state.paused ? "RESUME" : "PAUSE";
    }
    if (dom.pauseButton) {
      dom.pauseButton.setAttribute("aria-pressed", state.paused ? "true" : "false");
    }
  }

  function renderCoinModal() {
    var secs           = state.coinSessionActive ? state.coinCountdown : INSERT_TIMEOUT;
    var credits        = state.credits || 0;
    var insertedAmount = state.insertedAmount || 0;
    var mins           = credits * MINUTES_PER_PESO;

    if (dom.coinCountdownEl)  dom.coinCountdownEl.textContent  = String(secs);
    if (dom.coinProgressBar) {
      dom.coinProgressBar.style.width = (secs / INSERT_TIMEOUT * 100) + "%";
    }
    if (dom.coinTimeEl)       dom.coinTimeEl.textContent       = mins + "m";
    if (dom.coinVoucherTime)  dom.coinVoucherTime.textContent  = mins + "m";
    if (dom.insertedAmountEl) {
      dom.insertedAmountEl.textContent = (credits || insertedAmount).toFixed(2);
    }
    if (dom.coinNoteEl) {
      dom.coinNoteEl.textContent = state.coinSessionActive
        ? "Waiting for coin pulses from this device."
        : "Coin insert session is idle. Credits stay on this device.";
    }
  }

  function renderRatesModal(viewModel) {
    if (!dom.ratesList || !viewModel || !Array.isArray(viewModel.rates)) return;
    dom.ratesList.innerHTML = viewModel.rates.map(function (r) {
      return '<div class="rate-card">' +
               '<span>&#8369;' + r.peso + '</span>' +
               '<strong>' + r.minutes + ' mins</strong>' +
             '</div>';
    }).join("");
    if (viewModel.minutesPerPeso) {
      MINUTES_PER_PESO = viewModel.minutesPerPeso;
    }
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
    render();

    fetchSession()
      .then(function (session) {
        if (session) applyNormalizedSession(session);
        startMainTimer();
      })
      .catch(function () {
        startMainTimer();
      });

    bindEvents();
    loadBranding();
    connectBrandingEvents();
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
