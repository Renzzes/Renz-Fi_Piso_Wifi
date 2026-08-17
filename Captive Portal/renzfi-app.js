(function () {
  "use strict";

  var DEV_LOG =
    /[?&]debug=1/.test(window.location.search) ||
    /localhost|127\.0\.0\.1/.test(window.location.hostname);

  function log() {
    if (!DEV_LOG) return;
    var args = Array.prototype.slice.call(arguments);
    args.unshift("[portal]");
    console.log.apply(console, args);
  }

  function resolveApplianceBaseUrl() {
    var configured = "__RENZFI_APPLIANCE_BASE_URL__";
    var hasPlaceholder =
      !configured || configured.indexOf("__RENZFI_APPLIANCE_BASE_URL__") >= 0;
    if (!hasPlaceholder && /^https?:\/\//i.test(configured)) {
      return configured.replace(/\/+$/, "");
    }
    return window.location.origin.replace(/\/+$/, "");
  }

  var APPLIANCE_BASE_URL = resolveApplianceBaseUrl();
  var API_BASE = APPLIANCE_BASE_URL + "/api/portal";
  var EVENTS_URL = APPLIANCE_BASE_URL + "/api/events";
  var BRANDING_CACHE_KEY = "renzfi.portal.branding.v1";
  var SESSION_CACHE_PREFIX = "renzfi.portal.session.";
  var INSERT_TIMEOUT = 60;
  var COIN_POLL_MS = 2000;
  var HEARTBEAT_NORMAL_MS = 10000;
  var HEARTBEAT_BACKOFF_MS = [10000, 20000, 30000, 60000];
  var API_TIMEOUT_MS = 10000;

  var timers = {
    main: null,
    coin: null,
    coinPoll: null,
    heartbeat: null,
    clock: null
  };
  var heartbeatFailureCount = 0;
  var eventSource = null;
  var eventSourceConnecting = false;
  var donePayingInFlight = false;
  var cancelModalInFlight = false;
  var coinModalVisible = false;

  var dom = {};
  var state = {
    credits: 0,
    secondsLeft: 0,
    paused: false,
    connected: false,
    coinSessionActive: false,
    coinCountdown: INSERT_TIMEOUT,
    insertedAmount: 0,
    sessionState: "idle",
    offline: false
  };

  function textOf(id) {
    var el = document.getElementById(id);
    return el ? String(el.textContent || "").trim() : "";
  }

  function getDeviceMAC() {
    var mac = textOf("macAddress");
    return mac && mac.indexOf("$(") === -1 ? mac : "";
  }

  function getDeviceIP() {
    var ip = textOf("ipAddress");
    return ip && ip.indexOf("$(") === -1 ? ip : "";
  }

  function getSessionCacheKey() {
    var mac = getDeviceMAC();
    var ip = getDeviceIP();
    var key = (mac || ip || "unknown-device").replace(/[^a-z0-9._:-]/gi, "_");
    return SESSION_CACHE_PREFIX + key;
  }

  function deviceParams() {
    return { mac: getDeviceMAC(), ip: getDeviceIP() };
  }

  function safeJsonParse(text) {
    try {
      return JSON.parse(text);
    } catch (err) {
      return null;
    }
  }

  function friendlyError(kind) {
    if (kind === "timeout") return "Portal temporarily disconnected.";
    if (kind === "network") return "Portal temporarily disconnected.";
    if (kind === "invalid_json") return "Portal response is invalid.";
    return "Portal temporarily disconnected.";
  }

  function setOfflineMode(offline) {
    state.offline = !!offline;
    if (dom.serviceNotice) dom.serviceNotice.hidden = !offline;
    if (dom.statusEl && offline) {
      dom.statusEl.textContent = "Reconnecting...";
      dom.statusEl.classList.add("disconnected");
    }
  }

  function markApiSuccess() {
    heartbeatFailureCount = 0;
    setOfflineMode(false);
  }

  function markApiFailure() {
    setOfflineMode(true);
  }

  function scheduleHeartbeat(delayMs) {
    if (timers.heartbeat) clearTimeout(timers.heartbeat);
    timers.heartbeat = setTimeout(runHeartbeat, delayMs);
  }

  function nextHeartbeatDelay() {
    if (heartbeatFailureCount <= 0) return HEARTBEAT_NORMAL_MS;
    var idx = Math.min(heartbeatFailureCount - 1, HEARTBEAT_BACKOFF_MS.length - 1);
    return HEARTBEAT_BACKOFF_MS[idx];
  }

  function request(path, options) {
    options = options || {};
    var method = options.method || "GET";
    var timeoutMs = options.timeoutMs || API_TIMEOUT_MS;
    var retries = options.retries || 0;
    var body = options.body;
    var url = API_BASE + path;
    var attempt = 0;

    function once() {
      attempt += 1;
      var controller = new AbortController();
      var timeoutId = setTimeout(function () {
        controller.abort();
      }, timeoutMs);

      var fetchOptions = {
        method: method,
        cache: "no-store",
        signal: controller.signal,
        headers: { Accept: "application/json" }
      };
      if (body !== undefined) {
        fetchOptions.headers["Content-Type"] = "application/json";
        fetchOptions.body = JSON.stringify(body || {});
      }

      return fetch(url, fetchOptions)
        .then(function (response) {
          clearTimeout(timeoutId);
          return response.text().then(function (rawText) {
            var json = safeJsonParse(rawText);
            if (!json) {
              throw { kind: "invalid_json", message: friendlyError("invalid_json") };
            }
            if (!response.ok || json.success !== true) {
              throw {
                kind: "api",
                code: json.code || ("HTTP_" + response.status),
                message: json.error || "Request failed"
              };
            }
            return json.data;
          });
        })
        .catch(function (err) {
          clearTimeout(timeoutId);
          var shaped = err;
          if (!err || !err.kind) {
            if (err && err.name === "AbortError") {
              shaped = { kind: "timeout", message: friendlyError("timeout") };
            } else {
              shaped = { kind: "network", message: friendlyError("network") };
            }
          }
          if (attempt <= retries) {
            var retryDelay = Math.min(2000 * attempt, 6000);
            return new Promise(function (resolve) {
              setTimeout(resolve, retryDelay);
            }).then(once);
          }
          throw shaped;
        });
    }

    return once();
  }

  function normalizeSession(raw) {
    if (!raw || typeof raw !== "object") return null;
    var coinActive = raw.coinSessionActive;
    if (coinActive === undefined) coinActive = raw.coinWindowActive;
    var countdown = raw.coinCountdown;
    if (countdown === undefined) countdown = raw.coinWindowRemaining;
    return {
      secondsLeft: Number(raw.secondsLeft || 0) || 0,
      credits: Number(raw.credits || 0) || 0,
      paused: !!raw.paused,
      coinSessionActive: !!coinActive,
      coinCountdown: Number(countdown || INSERT_TIMEOUT) || INSERT_TIMEOUT,
      insertedAmount: Number(raw.insertedAmount || 0) || 0,
      sessionState: raw.sessionState || "idle",
      connected: !!raw.connected || Number(raw.secondsLeft || 0) > 0
    };
  }

  function normalizeRates(raw) {
    var rates = Array.isArray(raw) ? raw : raw && raw.rates;
    if (!Array.isArray(rates)) return [];
    return rates
      .filter(function (x) {
        return x && Number(x.coin || x.peso || 0) > 0 && Number(x.minutes || 0) > 0;
      })
      .map(function (x) {
        return {
          peso: Number(x.coin || x.peso || 0),
          minutes: Number(x.minutes || 0),
          speedProfile: x.speedProfile || "",
          deviceLimit: Number(x.deviceLimit || 0) || 0
        };
      })
      .sort(function (a, b) {
        return a.peso - b.peso;
      });
  }

  function saveSessionCache() {
    try {
      localStorage.setItem(getSessionCacheKey(), JSON.stringify(state));
    } catch (err) {}
  }

  function loadSessionCache() {
    try {
      var raw = localStorage.getItem(getSessionCacheKey());
      if (!raw) return;
      var cached = safeJsonParse(raw);
      if (!cached) return;
      state.credits = Number(cached.credits || 0);
      state.secondsLeft = Number(cached.secondsLeft || 0);
      state.paused = !!cached.paused;
      state.coinSessionActive = !!cached.coinSessionActive;
      state.coinCountdown = Number(cached.coinCountdown || INSERT_TIMEOUT);
      state.insertedAmount = Number(cached.insertedAmount || 0);
      state.sessionState = cached.sessionState || "idle";
      state.connected = !!cached.connected || state.secondsLeft > 0;
    } catch (err) {}
  }

  function applySession(session) {
    if (!session) return;
    var prevCredits = state.credits;
    state.secondsLeft = session.secondsLeft;
    state.credits = session.credits;
    state.paused = session.paused;
    state.coinSessionActive = session.coinSessionActive;
    state.coinCountdown = session.coinCountdown;
    state.insertedAmount = session.insertedAmount;
    state.sessionState = session.sessionState;
    state.connected = session.connected;
    saveSessionCache();
    if (typeof performance !== "undefined" &&
        Number(session.credits) !== Number(prevCredits)) {
      console.log(
        "[coin-latency] T8 session state updated credits=" +
          state.credits +
          " t=" +
          performance.now().toFixed(1)
      );
    }
    renderAll();
    if (typeof performance !== "undefined" &&
        Number(session.credits) !== Number(prevCredits) &&
        coinModalVisible) {
      console.log(
        "[coin-latency] T9 modal render credits=" +
          state.credits +
          " t=" +
          performance.now().toFixed(1)
      );
    }
    if (state.coinSessionActive && !coinModalVisible) startCoinSessionUI();
    if (!state.coinSessionActive && coinModalVisible) closeCoinModal(true);
  }

  function setBannerSrc(url) {
    if (!dom.bannerEl) return;
    var fallback = dom.bannerEl.getAttribute("data-default-src") || "Default-Banner.png";
    dom.bannerEl.onerror = function () {
      dom.bannerEl.onerror = null;
      dom.bannerEl.src = fallback;
    };
    dom.bannerEl.src = url || fallback;
  }

  function setMusicSrc(url) {
    if (!dom.bgMusic) return;
    var fallback = "bg_music.mp3";
    var nextSrc = url || fallback;
    if ((dom.bgMusic.getAttribute("src") || "").indexOf(nextSrc) >= 0) return;
    dom.bgMusic.src = nextSrc;
    dom.bgMusic.load();
  }

  function saveBrandingCache(data) {
    try {
      localStorage.setItem(BRANDING_CACHE_KEY, JSON.stringify(data || {}));
    } catch (err) {}
  }

  function loadBrandingCache() {
    try {
      return safeJsonParse(localStorage.getItem(BRANDING_CACHE_KEY) || "{}") || {};
    } catch (err) {
      return {};
    }
  }

  function applyBranding(data) {
    data = data || {};
    if (data.hasCustomBanner && data.bannerUrl) {
      setBannerSrc(data.bannerUrl);
    } else {
      setBannerSrc(null);
    }
    if (data.hasCustomMusic && data.musicUrl) {
      setMusicSrc(data.musicUrl);
    } else {
      setMusicSrc(null);
    }
  }

  function syncBranding() {
    return request("/branding", { method: "GET", retries: 1 })
      .then(function (data) {
        markApiSuccess();
        applyBranding(data);
        saveBrandingCache(data);
      })
      .catch(function () {
        markApiFailure();
        applyBranding(loadBrandingCache());
      });
  }

  function syncSession() {
    var params = deviceParams();
    if (!params.mac) return Promise.reject({ kind: "api", message: "Device MAC unavailable" });
    var q =
      "/session?mac=" +
      encodeURIComponent(params.mac) +
      "&ip=" +
      encodeURIComponent(params.ip || "");
    return request(q, { method: "GET", retries: 1 }).then(function (raw) {
      markApiSuccess();
      applySession(normalizeSession(raw));
    });
  }

  function syncRatesIntoModal() {
    return request("/rates", { method: "GET", retries: 1 })
      .then(function (raw) {
        markApiSuccess();
        renderRates(normalizeRates(raw));
      })
      .catch(function () {
        markApiFailure();
      });
  }

  function cacheDom() {
    dom.statusEl = document.getElementById("connectionStatus");
    dom.currentDateEl = document.getElementById("currentDate");
    dom.creditsEl = document.getElementById("credits");
    dom.mainTimerEl = document.getElementById("mainTimer");
    dom.insertCoinBtn = document.getElementById("insertCoinBtn");
    dom.insertCoinLabel = dom.insertCoinBtn
      ? dom.insertCoinBtn.querySelector(".action-label")
      : null;
    dom.pauseButton = document.getElementById("pauseButton");
    dom.pauseButtonText = document.getElementById("pauseButtonText");
    dom.viewRatesBtn = document.getElementById("viewRatesBtn");
    dom.ratesModal = document.getElementById("ratesModal");
    dom.ratesList = document.querySelector("#ratesModal .rates-list");
    dom.coinModal = document.getElementById("coinModal");
    dom.coinCountdownEl = document.getElementById("coinCountdown");
    dom.coinProgressEl = document.getElementById("coinProgressBar");
    dom.coinTimeEl = document.getElementById("coinTime");
    dom.coinVoucherEl = document.getElementById("coinVoucherTime");
    dom.insertedAmountEl = document.getElementById("insertedAmount");
    dom.coinNoteEl = document.getElementById("coinNote");
    dom.donePayingBtn = document.getElementById("donePayingBtn");
    dom.bannerEl = document.getElementById("portalBanner");
    dom.bgMusic = document.getElementById("bgMusic");
    dom.coinSound = document.getElementById("coinSound");
    dom.successSound = document.getElementById("successSound");
    dom.serviceNotice = document.getElementById("serviceNotice");
    dom.terminateBtn = document.getElementById("terminateBtn");
    dom.terminateModal = document.getElementById("terminateModal");
    dom.confirmTerminateBtn = document.getElementById("confirmTerminateBtn");
    dom.cancelTerminateBtn = document.getElementById("cancelTerminateBtn");
  }

  function renderStatus() {
    if (!dom.statusEl) return;
    if (state.offline) {
      dom.statusEl.textContent = "Reconnecting...";
      dom.statusEl.classList.add("disconnected");
      return;
    }
    var connected = state.connected || state.secondsLeft > 0;
    dom.statusEl.textContent = connected ? "Connected" : "Disconnected";
    dom.statusEl.classList.toggle("disconnected", !connected);
  }

  function renderCredits() {
    if (!dom.creditsEl) return;
    dom.creditsEl.textContent = "₱" + (Number(state.credits) || 0).toFixed(2);
  }

  function pad2(n) {
    return String(n).padStart(2, "0");
  }

  function renderTimer() {
    if (!dom.mainTimerEl) return;
    var s = Math.max(0, Number(state.secondsLeft) || 0);
    dom.mainTimerEl.textContent =
      pad2(Math.floor(s / 3600)) +
      ":" +
      pad2(Math.floor((s % 3600) / 60)) +
      ":" +
      pad2(s % 60);
  }

  function renderInsertButton() {
    if (!dom.insertCoinLabel) return;
    dom.insertCoinLabel.textContent = state.secondsLeft > 0 ? "ADD ADDITIONAL TIME" : "INSERT COIN";
  }

  function renderPauseButton() {
    if (dom.pauseButtonText) dom.pauseButtonText.textContent = state.paused ? "RESUME" : "PAUSE";
    if (dom.pauseButton) dom.pauseButton.setAttribute("aria-pressed", state.paused ? "true" : "false");
  }

  function renderCoinModal() {
    if (!dom.coinCountdownEl) return;
    var secs = Math.max(0, Number(state.coinCountdown) || 0);
    dom.coinCountdownEl.textContent = String(secs);
    if (dom.coinProgressEl) dom.coinProgressEl.style.width = String((secs / INSERT_TIMEOUT) * 100) + "%";
    if (dom.insertedAmountEl) dom.insertedAmountEl.textContent = (Number(state.insertedAmount) || 0).toFixed(2);
    if (dom.coinTimeEl) dom.coinTimeEl.textContent = String(Number(state.credits || 0) * 5) + "m";
    if (dom.coinVoucherEl) dom.coinVoucherEl.textContent = String(Number(state.credits || 0) * 5) + "m";
    if (dom.coinNoteEl) dom.coinNoteEl.textContent = state.coinSessionActive
      ? "Waiting for coin pulses from the machine."
      : "Coin insert session is idle.";
  }

  function renderDate() {
    if (!dom.currentDateEl) return;
    dom.currentDateEl.textContent = new Date().toLocaleString("en-PH", {
      weekday: "short",
      month: "short",
      day: "numeric",
      hour: "numeric",
      minute: "2-digit"
    });
  }

  function renderTerminateButton() {
    if (!dom.terminateBtn) return;
    dom.terminateBtn.hidden = !(state.secondsLeft > 0 || state.paused);
  }

  function renderAll() {
    renderStatus();
    renderCredits();
    renderTimer();
    renderInsertButton();
    renderPauseButton();
    renderCoinModal();
    renderTerminateButton();
    renderDate();
  }

  function playOnce(audioEl) {
    if (!audioEl) return;
    audioEl.currentTime = 0;
    audioEl.play().catch(function () {});
  }

  function startCoinSessionUI() {
    if (!dom.coinModal || coinModalVisible) return;
    coinModalVisible = true;
    dom.coinModal.hidden = false;
    document.body.style.overflow = "hidden";
    if (dom.bgMusic) {
      dom.bgMusic.currentTime = 0;
      dom.bgMusic.play().catch(function () {});
    }
    if (timers.coin) clearInterval(timers.coin);
    timers.coin = setInterval(function () {
      if (!state.coinSessionActive) return;
      state.coinCountdown = Math.max(0, Number(state.coinCountdown || 0) - 1);
      renderCoinModal();
      if (state.coinCountdown <= 0) {
        closeCoinModal(false);
      }
    }, 1000);
    if (timers.coinPoll) clearInterval(timers.coinPoll);
    timers.coinPoll = setInterval(function () {
      syncSession().catch(function () {});
    }, COIN_POLL_MS);
    renderCoinModal();
  }

  function closeCoinModal(skipCancelApi) {
    if (!dom.coinModal) return;
    dom.coinModal.hidden = true;
    coinModalVisible = false;
    if (!document.querySelector(".modal:not([hidden])")) document.body.style.overflow = "";
    if (timers.coin) clearInterval(timers.coin);
    timers.coin = null;
    if (timers.coinPoll) clearInterval(timers.coinPoll);
    timers.coinPoll = null;
    state.coinSessionActive = false;
    state.coinCountdown = INSERT_TIMEOUT;
    if (dom.bgMusic) {
      dom.bgMusic.pause();
      dom.bgMusic.currentTime = 0;
    }
    renderCoinModal();
    if (!skipCancelApi && !cancelModalInFlight) {
      cancelModalInFlight = true;
      request("/cancel-modal", { method: "POST", body: deviceParams(), retries: 0 })
        .then(function (raw) {
          markApiSuccess();
          applySession(normalizeSession(raw));
        })
        .catch(function () {
          markApiFailure();
        })
        .finally(function () {
          cancelModalInFlight = false;
        });
    }
  }

  function renderRates(rates) {
    if (!dom.ratesList) return;
    if (!rates || !rates.length) {
      dom.ratesList.innerHTML = '<p class="rates-loading">Rates unavailable.</p>';
      return;
    }
    dom.ratesList.innerHTML = rates
      .map(function (r) {
        var extras = "";
        if (r.speedProfile) extras += '<span class="rate-meta">📶 ' + String(r.speedProfile) + "</span>";
        if (r.deviceLimit > 0) {
          extras += '<span class="rate-meta">💻 Max ' + r.deviceLimit + " device" + (r.deviceLimit > 1 ? "s" : "") + "</span>";
        }
        return (
          '<div class="rate-card"><span>₱' +
          r.peso +
          "</span><strong>" +
          r.minutes +
          " mins</strong>" +
          (extras ? '<div class="rate-extras">' + extras + "</div>" : "") +
          "</div>"
        );
      })
      .join("");
  }

  function submitHotspotAuthentication() {
    var mac = getDeviceMAC();
    if (!mac) return false;
    var form = document.forms.login;
    if (!form || !form.username) return false;
    form.username.value = mac;
    if (form.password) form.password.value = "renzfi";
    try {
      if (typeof window.doLogin === "function" && document.sendin) {
        window.doLogin();
      } else {
        form.submit();
      }
      return true;
    } catch (err) {
      return false;
    }
  }

  function startCoinSession() {
    if (dom.insertCoinBtn) dom.insertCoinBtn.disabled = true;
    request("/start-coin-session", { method: "POST", body: deviceParams(), retries: 1 })
      .then(function (raw) {
        markApiSuccess();
        var session = normalizeSession(raw);
        applySession(session);
        startCoinSessionUI();
        playOnce(dom.coinSound);
      })
      .catch(function () {
        markApiFailure();
        renderStatus();
      })
      .finally(function () {
        if (dom.insertCoinBtn) dom.insertCoinBtn.disabled = false;
      });
  }

  function finishCoinPayment() {
    if (donePayingInFlight) return;
    donePayingInFlight = true;
    if (dom.donePayingBtn) dom.donePayingBtn.disabled = true;
    request("/done-paying", { method: "POST", body: deviceParams(), retries: 1 })
      .then(function (raw) {
        markApiSuccess();
        var session = normalizeSession(raw);
        applySession(session);
        playOnce(dom.successSound);
        closeCoinModal(true);
        startMainTimer();
        submitHotspotAuthentication();
      })
      .catch(function () {
        markApiFailure();
      })
      .finally(function () {
        donePayingInFlight = false;
        if (dom.donePayingBtn) dom.donePayingBtn.disabled = false;
      });
  }

  function togglePause() {
    if (dom.pauseButton) dom.pauseButton.disabled = true;
    var endpoint = state.paused ? "/resume" : "/pause";
    request(endpoint, { method: "POST", body: deviceParams(), retries: 1 })
      .then(function () {
        markApiSuccess();
        return syncSession();
      })
      .catch(function () {
        markApiFailure();
      })
      .finally(function () {
        if (dom.pauseButton) dom.pauseButton.disabled = false;
      });
  }

  function terminateSession() {
    if (dom.confirmTerminateBtn) dom.confirmTerminateBtn.disabled = true;
    request("/terminate", { method: "POST", body: deviceParams(), retries: 1 })
      .then(function (raw) {
        markApiSuccess();
        applySession(normalizeSession(raw));
        if (dom.terminateModal) dom.terminateModal.hidden = true;
      })
      .catch(function () {
        markApiFailure();
      })
      .finally(function () {
        if (dom.confirmTerminateBtn) dom.confirmTerminateBtn.disabled = false;
      });
  }

  function runHeartbeat() {
    request("/heartbeat", { method: "POST", body: deviceParams(), retries: 0, timeoutMs: 8000 })
      .then(function () {
        return syncSession();
      })
      .then(function () {
        heartbeatFailureCount = 0;
        scheduleHeartbeat(HEARTBEAT_NORMAL_MS);
      })
      .catch(function () {
        heartbeatFailureCount += 1;
        markApiFailure();
        scheduleHeartbeat(nextHeartbeatDelay());
      });
  }

  function ensureSingleEventSource() {
    if (eventSource || eventSourceConnecting) return;
    eventSourceConnecting = true;
    try {
      eventSource = new EventSource(EVENTS_URL);
      eventSource.onopen = function () {
        eventSourceConnecting = false;
        markApiSuccess();
      };
      eventSource.onerror = function () {
        eventSourceConnecting = false;
        markApiFailure();
      };
      eventSource.addEventListener("portal.changed", function () {
        syncBranding();
      });
      // Forensic instrumentation only — does not apply session payload.
      // Proves whether portal.coin.credit reaches the browser while the modal
      // currently refreshes via sessions.changed → syncSession() HTTP path.
      eventSource.addEventListener("portal.coin.credit", function (ev) {
        if (typeof performance === "undefined") return;
        console.log(
          "[coin-latency] T7 portal.coin.credit SSE received t=" +
            performance.now().toFixed(1) +
            " bytes=" +
            ((ev && ev.data && ev.data.length) || 0) +
            " (listener does not apply — modal uses sessions.changed sync)"
        );
      });
      eventSource.addEventListener("sessions.changed", function () {
        if (typeof performance !== "undefined") {
          console.log(
            "[coin-latency] sessions.changed → syncSession t=" +
              performance.now().toFixed(1)
          );
        }
        syncSession().catch(function () {});
      });
    } catch (err) {
      eventSource = null;
      eventSourceConnecting = false;
      markApiFailure();
    }
  }

  function bindEvents() {
    if (dom.insertCoinBtn) dom.insertCoinBtn.addEventListener("click", startCoinSession);
    if (dom.donePayingBtn) dom.donePayingBtn.addEventListener("click", finishCoinPayment);
    if (dom.pauseButton) dom.pauseButton.addEventListener("click", togglePause);
    if (dom.viewRatesBtn) {
      dom.viewRatesBtn.addEventListener("click", function () {
        syncRatesIntoModal().finally(function () {
          if (dom.ratesModal) {
            dom.ratesModal.hidden = false;
            document.body.style.overflow = "hidden";
          }
        });
      });
    }
    if (dom.terminateBtn && dom.terminateModal) {
      dom.terminateBtn.addEventListener("click", function () {
        dom.terminateModal.hidden = false;
        document.body.style.overflow = "hidden";
      });
    }
    if (dom.cancelTerminateBtn && dom.terminateModal) {
      dom.cancelTerminateBtn.addEventListener("click", function () {
        dom.terminateModal.hidden = true;
        if (!document.querySelector(".modal:not([hidden])")) document.body.style.overflow = "";
      });
    }
    if (dom.confirmTerminateBtn) dom.confirmTerminateBtn.addEventListener("click", terminateSession);

    document.addEventListener("click", function (event) {
      if (event.target.closest("[data-close-modal]")) {
        var modal = event.target.closest(".modal");
        if (modal === dom.coinModal) closeCoinModal(false);
        else {
          modal.hidden = true;
          if (!document.querySelector(".modal:not([hidden])")) document.body.style.overflow = "";
        }
      } else if (event.target.classList && event.target.classList.contains("modal")) {
        if (event.target === dom.coinModal) closeCoinModal(false);
        else {
          event.target.hidden = true;
          if (!document.querySelector(".modal:not([hidden])")) document.body.style.overflow = "";
        }
      }
    });

    window.addEventListener("unhandledrejection", function (event) {
      event.preventDefault();
      log("suppressed unhandled rejection");
    });
  }

  function startMainTimer() {
    if (timers.main) clearInterval(timers.main);
    timers.main = setInterval(function () {
      if (state.secondsLeft > 0 && !state.paused) {
        state.secondsLeft -= 1;
        if (state.secondsLeft < 0) state.secondsLeft = 0;
        renderTimer();
        renderStatus();
        saveSessionCache();
      }
    }, 1000);
  }

  function init() {
    cacheDom();
    loadSessionCache();
    renderAll();

    applyBranding(loadBrandingCache());
    syncBranding();
    syncSession().catch(function () {
      markApiFailure();
    });
    startMainTimer();
    bindEvents();
    ensureSingleEventSource();
    scheduleHeartbeat(HEARTBEAT_NORMAL_MS);

    if (timers.clock) clearInterval(timers.clock);
    timers.clock = setInterval(renderDate, 1000);
    renderDate();
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
