#pragma once

const char kSetupWizardPageHtml[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Renz-Fi Setup</title>
  <style>
    *{box-sizing:border-box}
    body{font-family:system-ui,-apple-system,sans-serif;margin:0;background:#0f172a;color:#e2e8f0;
         min-height:100vh;padding:20px 16px 32px}
    main{max-width:480px;margin:0 auto}
    h1{margin:0 0 4px;font-size:1.5rem;font-weight:700}
    .sub{color:#94a3b8;margin:0 0 16px;font-size:.875rem;line-height:1.4}
    .steps{display:flex;gap:4px;margin-bottom:20px}
    .step{flex:1;height:4px;border-radius:2px;background:#334155}
    .step.active{background:#3b82f6}
    .step.done{background:#22c55e}
    .card{background:#1e293b;border-radius:12px;padding:16px;margin-bottom:16px}
    .card h2{margin:0 0 12px;font-size:1rem;font-weight:600}
    .row{display:flex;justify-content:space-between;align-items:flex-start;
         padding:8px 0;border-bottom:1px solid #334155;font-size:.9rem;gap:12px}
    .row:last-child{border-bottom:none}
    .row span:first-child{color:#94a3b8;flex-shrink:0}
    .row span:last-child{font-weight:600;text-align:right;word-break:break-all}
    .ok{color:#4ade80}
    .warn{color:#fbbf24}
    .notice{background:#172554;color:#bfdbfe;border-radius:8px;padding:12px;
            font-size:.85rem;line-height:1.5;margin-bottom:16px}
    .session-bar{display:none;background:#1e293b;border:1px solid #334155;border-radius:8px;
                 padding:10px 12px;margin-bottom:12px;font-size:.85rem;color:#cbd5e1;
                 align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}
    .session-bar.visible{display:flex}
    .session-bar button{width:auto;margin:0;padding:8px 12px;font-size:.8rem}
    .successbox{background:#14532d;color:#bbf7d0;border-radius:8px;padding:14px;
                font-size:.9rem;line-height:1.5;margin-bottom:16px}
    .form-error{background:#450a0a;color:#fecaca;border-radius:8px;padding:10px 12px;
                font-size:.85rem;margin-bottom:12px;display:none}
    label{display:block;font-size:.85rem;color:#94a3b8;margin:0 0 4px}
    input,select{width:100%;background:#0f172a;border:1px solid #475569;border-radius:8px;
          color:#e2e8f0;padding:10px 12px;font-size:1rem;margin-bottom:12px}
    input:focus,select:focus{outline:none;border-color:#3b82f6}
    input.inline-num{width:88px;margin:0 0 0 8px;display:inline-block}
    button{width:100%;background:#2563eb;color:#fff;border:none;border-radius:8px;
            padding:12px 16px;font-size:.95rem;font-weight:600;cursor:pointer;margin-top:4px}
    button.secondary{background:#334155;margin-top:8px}
    button:disabled{opacity:.45;cursor:not-allowed}
    button:active:not(:disabled){opacity:.85}
    .panel{display:none}
    .panel.active{display:block}
    .loading{color:#94a3b8;font-size:.85rem;text-align:center;padding:8px}
    code{background:#334155;padding:2px 6px;border-radius:4px;font-size:.85rem}
    details{margin:12px 0;background:#0f172a;border-radius:8px;padding:10px 12px}
    details summary{cursor:pointer;font-weight:600;color:#cbd5e1}
    .action-list{font-size:.82rem;line-height:1.45;margin-top:8px}
    .action-row{padding:8px 0;border-bottom:1px solid #334155}
    .action-row:last-child{border-bottom:none}
    .conflict{color:#fca5a5}
    .field-hint{font-size:.78rem;color:#94a3b8;margin:-6px 0 12px;line-height:1.4}
    .ap-choice-group{display:flex;flex-direction:column;gap:8px;margin-bottom:12px;width:100%;max-width:100%}
    .ap-choice{display:flex;align-items:flex-start;gap:10px;width:100%;max-width:100%;min-width:0;
               box-sizing:border-box;padding:12px;border:1px solid #475569;border-radius:8px;
               background:#0f172a;cursor:pointer;margin:0}
    .ap-choice input[type=radio]{margin:3px 0 0;accent-color:#3b82f6;flex-shrink:0;width:auto}
    .ap-choice-text{flex:1;min-width:0;max-width:100%;overflow-wrap:anywhere;word-break:normal;
                    font-size:.9rem;line-height:1.45;color:#cbd5e1}
    .ap-choice-text strong{display:block;color:#e2e8f0;margin-bottom:4px;font-weight:600}
    .deferred-section{margin-top:12px;font-size:.82rem;line-height:1.45}
    .deferred-section ul{margin:8px 0 0;padding-left:18px}
    .network-mode-group{display:flex;flex-direction:column;gap:8px;margin-bottom:16px;width:100%;max-width:100%}
    .network-mode-choice{display:flex;align-items:flex-start;gap:10px;width:100%;max-width:100%;min-width:0;
                         box-sizing:border-box;padding:12px;border:1px solid #475569;border-radius:8px;
                         background:#0f172a;cursor:pointer;margin:0}
    .network-mode-choice input[type=radio]{margin:3px 0 0;accent-color:#3b82f6;flex-shrink:0;width:auto}
    .network-mode-choice-text{flex:1;min-width:0;max-width:100%;overflow-wrap:anywhere;word-break:normal;
                              font-size:.9rem;line-height:1.45;color:#cbd5e1}
    .network-mode-choice-text strong{display:block;color:#e2e8f0;margin-bottom:4px;font-weight:600}
    .candidate-card{border:1px solid #475569;border-radius:8px;padding:12px;margin-bottom:8px;background:#0f172a;
                      width:100%;max-width:100%;box-sizing:border-box}
    .candidate-card.selected{border-color:#3b82f6;background:#172554}
    .hidden-block{display:none}
    .scan-progress{display:flex;align-items:center;gap:12px;margin:12px 0;padding:12px;
                   background:#0f172a;border-radius:8px;border:1px solid #334155}
    .scan-progress-spinner{width:18px;height:18px;border:2px solid #334155;border-top-color:#3b82f6;
                           border-radius:50%;animation:spin .8s linear infinite;flex-shrink:0}
    @keyframes spin{to{transform:rotate(360deg)}}
    .scan-summary{margin-top:12px}
    .scan-checklist{margin:8px 0 0;padding-left:0;list-style:none}
    .scan-checklist li{padding:4px 0;color:#bbf7d0;font-size:.88rem}
    .modal-backdrop{position:fixed;inset:0;background:rgba(15,23,42,.82);display:flex;
                    align-items:center;justify-content:center;padding:16px;z-index:1000}
    .modal-backdrop.hidden-block{display:none}
    .modal-card{background:#1e293b;border-radius:12px;padding:16px;max-width:420px;width:100%;
                max-height:90vh;overflow:auto;border:1px solid #475569}
    .modal-actions{display:flex;gap:8px;margin-top:16px}
    .modal-actions button{flex:1;margin-top:0}
    .verify-step{padding:6px 0;font-size:.88rem;color:#94a3b8}
    .verify-step.ok{color:#4ade80}
    .install-summary{margin-top:4px}
    .install-divider{height:1px;background:#334155;margin:0}
    .install-summary-heading{text-align:center;font-weight:700;font-size:.95rem;
                             margin:10px 0;letter-spacing:.02em}
    .install-section{padding:12px 0;border-top:1px solid #334155}
    .install-section-label{font-size:.78rem;color:#94a3b8;text-transform:uppercase;
                           letter-spacing:.04em;margin-bottom:6px}
    .install-section-value{font-size:.95rem;font-weight:600;line-height:1.4}
    .install-section-detail{font-size:.85rem;color:#cbd5e1;margin-top:4px}
    .install-section-ok{color:#4ade80;font-size:.85rem;margin-top:4px}
    .dashboard-address{font-family:ui-monospace,monospace;font-size:.88rem;word-break:break-all;
                       margin:4px 0 0;padding:10px 12px;background:#0f172a;border-radius:8px;
                       border:1px solid #334155;color:#e2e8f0}
    .install-important{margin:12px 0;padding:12px;background:#172554;border-radius:8px;
                      font-size:.82rem;line-height:1.5;color:#bfdbfe}
    .install-important strong{display:block;margin-bottom:4px;color:#e2e8f0}
    .install-actions{display:flex;flex-direction:column;gap:8px;margin-top:16px}
    .install-actions button.secondary{margin-top:0}
    .busy-overlay{position:fixed;inset:0;background:rgba(15,23,42,.88);display:flex;
                  align-items:center;justify-content:center;padding:20px;z-index:2000}
    .busy-overlay.hidden-block{display:none}
    .busy-card{background:#1e293b;border:1px solid #475569;border-radius:14px;padding:22px 18px;
               max-width:360px;width:100%;text-align:center;box-shadow:0 12px 40px rgba(0,0,0,.45)}
    .busy-spinner{width:36px;height:36px;margin:0 auto 14px;border:3px solid #334155;
                  border-top-color:#3b82f6;border-radius:50%;animation:spin .75s linear infinite}
    .busy-card h2{margin:0 0 8px;font-size:1.05rem;font-weight:700;color:#e2e8f0}
    .busy-detail{margin:0 0 16px;font-size:.9rem;line-height:1.45;color:#cbd5e1;min-height:2.6em}
    .busy-progress-track{height:8px;background:#0f172a;border-radius:999px;overflow:hidden;
                         border:1px solid #334155;margin-bottom:12px}
    .busy-progress-bar{height:100%;width:18%;background:linear-gradient(90deg,#2563eb,#38bdf8);
                       border-radius:999px;transition:width .35s ease}
    .busy-progress-bar.indeterminate{width:40%;animation:busy-slide 1.2s ease-in-out infinite}
    @keyframes busy-slide{0%{transform:translateX(-120%)}100%{transform:translateX(280%)}}
    .busy-hint{margin:0;font-size:.78rem;line-height:1.4;color:#94a3b8}
    .busy-actions{display:flex;gap:8px;margin-top:14px}
    .busy-actions button{flex:1;margin-top:0}
    .busy-card.busy-error .busy-spinner{display:none}
    .busy-card.busy-error .busy-progress-track{display:none}
  </style>
</head>
<body>
  <main>
    <h1>Renz-Fi Setup</h1>
    <p class="sub">Management Wi-Fi &middot; <code>192.168.4.1</code></p>
    <div id="setupSessionBar" class="session-bar" aria-live="polite">
      <span id="setupSessionLabel">Setup unlocked temporarily</span>
      <button id="cancelSetupBtn" type="button" class="secondary">Cancel</button>
    </div>
    <div class="notice">
      Use this network for first-time setup only. The full dashboard is available
      on the MikroTik network after setup is complete.
    </div>
    <div class="steps">
      <div id="stepBar1" class="step active"></div>
      <div id="stepBar2" class="step"></div>
      <div id="stepBar3" class="step"></div>
      <div id="stepBar4" class="step"></div>
    </div>

    <div id="panelOwner" class="panel active">
      <div class="card">
        <h2>Step 1 &mdash; Owner Account</h2>
        <p class="sub" style="margin-bottom:12px">
          Create the appliance owner (Super Administrator). This account has full access to the system.
        </p>
        <div id="ownerFormError" class="form-error"></div>
        <label for="ownerDisplayName">Full name</label>
        <input id="ownerDisplayName" type="text" autocomplete="name" maxlength="64" placeholder="Your name">
        <label for="username">Username</label>
        <input id="username" type="text" autocomplete="username" maxlength="32" placeholder="owner">
        <label for="password">Password</label>
        <input id="password" type="password" autocomplete="new-password" placeholder="At least 8 characters">
        <label for="confirmPassword">Confirm password</label>
        <input id="confirmPassword" type="password" autocomplete="new-password" placeholder="Repeat password">
        <label for="setupUnlockPassword">Setup Unlock Password</label>
        <input id="setupUnlockPassword" type="password" autocomplete="new-password" placeholder="Required for future setup changes">
        <label for="confirmSetupUnlockPassword">Confirm Setup Unlock Password</label>
        <input id="confirmSetupUnlockPassword" type="password" autocomplete="new-password" placeholder="Repeat setup unlock password">
        <button id="createBtn" type="button">Next</button>
      </div>
    </div>

    <div id="panelMikrotik" class="panel">
      <div class="card">
        <h2>Step 2 &mdash; Router Connection</h2>
        <p class="sub" style="margin-bottom:12px">
          Connect Renz-Fi to your MikroTik for hotspot, voucher, and rate management.
          This step only validates API access &mdash; no RouterOS changes are made yet.
        </p>
        <div class="row"><span>Ethernet link</span><span id="mikrotikLink">Checking&hellip;</span></div>
        <div class="row"><span>ESP32 IP</span><span id="mikrotikEspIp">Checking&hellip;</span></div>
        <div class="row"><span>Gateway</span><span id="mikrotikGateway">Checking&hellip;</span></div>
        <div id="routerFormError" class="form-error"></div>
        <div id="routerFormSuccess" class="successbox" style="display:none"></div>
        <label for="routerHost">Router IP address</label>
        <input id="routerHost" type="text" inputmode="decimal" autocomplete="off" placeholder="10.10.10.1">
        <label for="routerUsername">Router username</label>
        <input id="routerUsername" type="text" autocomplete="username" placeholder="admin">
        <label for="routerPassword">Router password</label>
        <input id="routerPassword" type="password" autocomplete="current-password" placeholder="MikroTik password">
        <p class="field-hint">Enter the MikroTik password for Test Connection and Save. Saved credentials are used from Step 3 onward.</p>
        <label for="routerPort">API port</label>
        <input id="routerPort" type="number" inputmode="numeric" min="1" max="65535" value="8728">
        <button id="testRouterBtn" type="button">Test Connection</button>
        <button id="saveRouterBtn" type="button" class="secondary" disabled>Save Router Connection</button>
        <button id="mikrotikBackBtn" type="button" class="secondary">Back</button>
      </div>
      <div id="routerSaveSuccessModal" class="modal-backdrop hidden-block" role="dialog" aria-modal="true">
        <div class="modal-card">
          <p class="notice" style="margin:0;display:flex;align-items:center;gap:8px">
            <span aria-hidden="true">&#10003;</span> Router connection saved
          </p>
          <p class="sub" style="margin:10px 0 0">Preparing your appliance&hellip;</p>
        </div>
      </div>
    </div>

    <div id="panelReview" class="panel">
      <div class="card">
        <h2>Step 3 &mdash; Router Scan</h2>
        <p class="sub" style="margin-bottom:12px">
          Renz-Fi checks your router configuration once. No changes are made until you confirm.
        </p>

        <div id="existingNetworkPanel">
          <div id="existingScanProgress" class="scan-progress" style="display:none">
            <div class="scan-progress-spinner" aria-hidden="true"></div>
            <div id="existingScanProgressLabel">Connecting...</div>
          </div>
          <div id="existingScanStatus" class="notice">Checking router configuration&hellip;</div>
          <div id="existingScanSummary" class="scan-summary" style="display:none"></div>
          <div id="existingScanResults" class="action-list" style="display:none;margin-top:12px"></div>
          <div id="existingScanError" class="form-error"></div>
          <button id="scanExistingBtn" type="button" class="secondary">Rescan</button>
          <button id="adoptExistingBtn" type="button" disabled>Confirm</button>
        </div>

        <button id="reviewBackBtn" type="button" class="secondary">Back</button>
      </div>
    </div>

    <div id="panelWifi" class="panel">
      <div class="card">
        <h2 id="wifiStepTitle">Step 4 &mdash; Wi-Fi Configuration</h2>
        <p id="wifiStepSub" class="sub" style="margin-bottom:12px">
          Choose an existing SSID on your MikroTik or create a dedicated Piso Wi-Fi network.
        </p>
        <div id="wifiFormError" class="form-error"></div>
        <div id="wifiNetworksNotice" class="notice">Loading available SSIDs&hellip;</div>

        <div id="wifiExternalApSection" style="display:none">
          <div class="notice" style="margin-bottom:12px">
            <strong>External Access Point</strong><br>
            No MikroTik Wi-Fi. Renz-Fi uses the guest bridge and HotSpot; Wi-Fi comes from a LAN AP.
          </div>
          <ul class="scan-checklist" style="margin:0 0 12px;padding-left:18px">
            <li>Guest bridge: <strong id="externalApBridgeLabel">&mdash;</strong></li>
            <li>Guest network: <strong id="externalApNetworkLabel">&mdash;</strong></li>
            <li>HotSpot / DHCP / NAT on MikroTik</li>
          </ul>
          <p class="sub" style="margin-bottom:12px">
            Register the AP after install under <strong>Networking &rarr; Access Points</strong>.
            Configure the AP first (bridge mode, DHCP off, NAT off).
          </p>
        </div>

        <div id="wifiMikrotikWirelessSection">
        <div class="network-mode-group">
          <label class="network-mode-choice">
            <input type="radio" name="wifiModeChoice" id="wifiModeExisting" value="existing" checked>
            <span class="network-mode-choice-text">
              <strong>Use Existing SSID (Recommended)</strong>
              Available SSIDs
              <select id="wifiExistingSelect" disabled style="margin-top:8px">
                <option value="">Loading&hellip;</option>
              </select>
            </span>
          </label>
          <label class="network-mode-choice">
            <input type="radio" name="wifiModeChoice" id="wifiModeNew" value="new">
            <span class="network-mode-choice-text">
              <strong>Create New SSID</strong>
              SSID Name
              <input id="wifiNewSsid" type="text" maxlength="32" placeholder="RENZ-FI" disabled>
              <span class="sub" style="display:block;margin-top:8px;font-size:0.9rem">
                Open network — no WPA password. Customer authentication uses the MikroTik Hotspot captive portal.
              </span>
            </span>
          </label>
        </div>
        </div>

        <button id="wifiNextBtn" type="button">Finish</button>
        <button id="wifiBackBtn" type="button" class="secondary">Back</button>
      </div>
    </div>

    <div id="panelProvisioned" class="panel">
      <div class="card">
        <h2 id="panelProvisionedTitle">Step 5 &mdash; Administrator &amp; Operator</h2>
        <p id="panelProvisionedSub" class="sub" style="margin-bottom:16px">
          Your administrator account is ready. Optionally create an operator account for staff use.
        </p>
        <ul id="setupCompleteChecklist" class="scan-checklist hidden-block" style="margin-bottom:16px">
          <li>&#10003; Owner Created</li>
          <li>&#10003; Router Connected</li>
          <li>&#10003; Existing Network Configured</li>
          <li>&#10003; Captive Portal Ready</li>
        </ul>
        <div id="productionReadyNotice" class="successbox hidden-block"></div>
        <div id="operatorOptionalSection">
          <div id="portalDeploymentChoice" style="margin-bottom:16px">
            <p class="field-hint" style="margin-top:0"><strong>Captive portal on Finish</strong></p>
            <p class="sub" style="margin-bottom:8px">
              Choose whether Finish verifies portal files on MikroTik. This is separate from creating an Operator account.
            </p>
            <label class="network-mode-choice" style="display:block;margin-bottom:8px">
              <input type="radio" name="portalDeploy" id="portalDeploySkip" value="skipped" checked>
              <span class="network-mode-choice-text">
                <strong>Skip portal verification</strong>
                Manual deploy later — avoids MikroTik file queries during Finish.
              </span>
            </label>
            <label class="network-mode-choice" style="display:block;margin-bottom:8px">
              <input type="radio" name="portalDeploy" id="portalDeployVerify" value="manual_external">
              <span class="network-mode-choice-text">
                <strong>Verify portal on Finish</strong>
                Targeted check for login.html on the Hotspot html-directory.
              </span>
            </label>
          </div>
          <p class="field-hint" style="margin-top:0"><strong>Optional</strong> &mdash; Create an Operator account</p>
          <p class="sub" style="margin-bottom:8px">
            Operator accounts can manage:
          </p>
          <ul class="scan-checklist" style="margin:0 0 12px;padding-left:18px">
            <li style="color:#94a3b8">Dashboard</li>
            <li style="color:#94a3b8">Rates</li>
            <li style="color:#94a3b8">Promo Rates</li>
            <li style="color:#94a3b8">Banner</li>
            <li style="color:#94a3b8">Captive Portal</li>
          </ul>
          <p class="sub" style="margin-bottom:8px">They cannot change:</p>
          <ul class="scan-checklist" style="margin:0 0 12px;padding-left:18px">
            <li style="color:#94a3b8">Vouchers</li>
            <li style="color:#94a3b8">Active Users</li>
            <li style="color:#94a3b8">Reports</li>
            <li style="color:#94a3b8">Network</li>
            <li style="color:#94a3b8">Router</li>
            <li style="color:#94a3b8">Firmware</li>
            <li style="color:#94a3b8">System Settings</li>
          </ul>
          <p class="sub" style="margin-bottom:12px">
            Operator accounts can also be created later from Admin Dashboard &rarr; System Settings.
          </p>
          <div id="operatorIntroActions">
            <button id="skipOperatorBtn" type="button" class="secondary">Skip Operator &amp; Finish</button>
            <button id="showOperatorFormBtn" type="button">Create Operator</button>
          </div>
          <div id="operatorFormSection" class="hidden-block" style="margin-top:12px">
            <div id="operatorFormError" class="form-error"></div>
            <label for="operatorDisplayName">Full name</label>
            <input id="operatorDisplayName" type="text" autocomplete="name" maxlength="64" placeholder="Staff name">
            <label for="operatorUsername">Username</label>
            <input id="operatorUsername" type="text" autocomplete="username" maxlength="32" placeholder="operator">
            <label for="operatorPassword">Password</label>
            <input id="operatorPassword" type="password" autocomplete="new-password" placeholder="At least 8 characters">
            <label for="operatorConfirmPassword">Confirm password</label>
            <input id="operatorConfirmPassword" type="password" autocomplete="new-password" placeholder="Repeat password">
            <p class="field-hint">Role: <strong>Operator</strong> (default permissions)</p>
            <button id="createOperatorBtn" type="button">Create Operator</button>
            <button id="operatorFormBackBtn" type="button" class="secondary">Back</button>
          </div>
          <button id="provisionedBackBtn" type="button" class="secondary">Back</button>
        </div>
      </div>
    </div>

    <div id="globalLoading" class="loading" style="display:none">Loading&hellip;</div>
  </main>

  <div id="busyOverlay" class="busy-overlay hidden-block" role="dialog" aria-modal="true"
       aria-labelledby="busyTitle" aria-describedby="busyDetail">
    <div id="busyCard" class="busy-card">
      <div class="busy-spinner" aria-hidden="true"></div>
      <h2 id="busyTitle">Working&hellip;</h2>
      <p id="busyDetail" class="busy-detail">Please wait.</p>
      <div class="busy-progress-track" aria-hidden="true">
        <div id="busyProgressBar" class="busy-progress-bar indeterminate"></div>
      </div>
      <p id="busyHint" class="busy-hint">
        Do not close this page or leave the Management Wi-Fi network.
      </p>
      <div id="busyActions" class="busy-actions hidden-block"></div>
    </div>
  </div>

  <script>
    var setupStatus = null;
    var submitting = false;
    var activePanel = 'panelOwner';
    var setupSessionEndsAt = 0;
    var setupSessionTimerId = null;
    var setupSessionLocking = false;

    var routerSubmitting = false;
    var routerTestOk = false;
    var routerConnectionId = null;
    var routerTestSnapshot = null;
    var routerHasSavedPassword = false;
    var operatorSubmitting = false;
    var finishSetupSubmitting = false;
    var setupCompleteAckSubmitting = false;
    var dashboardRedirectStarted = false;
    var scanSubmitting = false;
    var applyExistingNetworkInFlight = false;
    var wifiAdoptionDmaRetries = 0;
    var wifiNetworksLoading = false;
    var wifiNetworksLoaded = false;
    var wifiDiscoveryRetryTimer = null;
    var wifiSaveInFlight = false;
    var wifiSelection = { mode: 'existing', interfaceId: '', ssid: '', password: '', selectedSsid: '' };
    var externalApWizardMode = false;
    var availableWifiNetworks = [];
    var currentJobState = 'idle';
    var currentJobType = null;
    var activeExistingScanJobId = null;
    var selectedExistingCandidate = null;
    var networkModeState = null;
    var lastExistingScanData = null;
    // Set only when a device restart has been positively confirmed via a
    // changed bootInstanceId (see confirmExistingScanRestart()). Never set
    // from a single failed/missing-job poll alone.
    var existingScanRestartFlagged = false;

    // Monotonic counter for scan-state writes (used by stale-update guards).
    var scanTraceId = 0;
    var existingScanStateUpdateCounter = 0;

    function setCurrentJobState(newState) {
      if (newState !== currentJobState) {
        currentJobState = newState;
      }
    }

    function setCurrentJobType(newType) {
      if (newType !== currentJobType) {
        currentJobType = newType;
      }
    }

    function parseScanJobId(scanId) {
      if (!scanId || typeof scanId !== 'string') return 0;
      var match = scanId.match(/^scan-(\d+)$/);
      return match ? parseInt(match[1], 10) : 0;
    }

    function evaluateExistingScanUpdate(current, incoming) {
      if (incoming === null || incoming === undefined) {
        return { accept: true, reason: 'clear requested' };
      }
      if (!current) {
        return { accept: true, reason: 'no current state' };
      }

      var currentJobId = parseScanJobId(current.scanId);
      var incomingJobId = parseScanJobId(incoming.scanId);
      if (incomingJobId > 0 && currentJobId > 0) {
        if (incomingJobId > currentJobId) {
          return { accept: true, reason: 'incoming scanId is newer' };
        }
        if (incomingJobId < currentJobId) {
          return { accept: false, reason: 'incoming scanId is stale' };
        }
      } else if (incoming.scanId && current.scanId && incoming.scanId !== current.scanId) {
        if (String(incoming.scanId) > String(current.scanId)) {
          return { accept: true, reason: 'incoming scanId is newer' };
        }
        if (String(incoming.scanId) < String(current.scanId)) {
          return { accept: false, reason: 'incoming scanId is stale' };
        }
      }

      var currentCompletedAt = current.completedAt || '';
      var incomingCompletedAt = incoming.completedAt || '';
      if (incomingCompletedAt && currentCompletedAt) {
        if (incomingCompletedAt > currentCompletedAt) {
          return { accept: true, reason: 'incoming completedAt is newer' };
        }
        if (incomingCompletedAt < currentCompletedAt) {
          return { accept: false, reason: 'incoming completedAt is stale' };
        }
      }

      var currentCounter = current._stateUpdateCounter || 0;
      var incomingCounter = incoming._stateUpdateCounter || 0;
      if (incomingCounter > currentCounter) {
        return { accept: true, reason: 'incoming update counter is newer' };
      }
      if (incomingCounter < currentCounter) {
        return { accept: false, reason: 'incoming update counter is stale' };
      }

      return { accept: false, reason: 'duplicate stale update' };
    }

    // Single authoritative writer for lastExistingScanData.
    function updateExistingScanState(source, incomingData) {
      var currentScanId = lastExistingScanData && lastExistingScanData.scanId || null;
      var incomingScanId = incomingData && incomingData.scanId || null;
      var decision = evaluateExistingScanUpdate(lastExistingScanData, incomingData);

      if (!decision.accept) {
        return false;
      }

      existingScanStateUpdateCounter++;
      if (incomingData === null || incomingData === undefined) {
        lastExistingScanData = null;
      } else {
        lastExistingScanData = Object.assign({}, incomingData);
        lastExistingScanData._stateUpdateCounter = existingScanStateUpdateCounter;
      }
      return true;
    }
    var savedRouterHost = '';
    var setupCompleteShown = false;
    var scanActivityGeneration = 0;

    // Display/diagnostics only — wizard navigation uses data.wizardStep from the backend.
    function isExistingNetworkConfigured(status) {
      status = status || setupStatus || {};
      var np = status.networkProvisioning || networkModeState || {};
      return !!(np.existingNetworkAdopted || np.foundationApplied);
    }

    // Display/diagnostics only — wizard navigation uses data.wizardStep from the backend.
    function isWifiSetupComplete(status) {
      status = status || setupStatus || {};
      var np = status.networkProvisioning || {};
      if (np.wifiSetupComplete) return true;
      if (np.externalApOnly && np.wifiSelectionConfigured) return true;
      return !!(np.wifiSelectionConfigured && np.interfaceId);
    }

    function setExternalApWizardMode(on) {
      externalApWizardMode = !!on;
      var ext = document.getElementById('wifiExternalApSection');
      var mik = document.getElementById('wifiMikrotikWirelessSection');
      var title = document.getElementById('wifiStepTitle');
      var sub = document.getElementById('wifiStepSub');
      if (ext) ext.style.display = on ? 'block' : 'none';
      if (mik) mik.style.display = on ? 'none' : 'block';
      if (title) {
        title.textContent = on
          ? 'Step 4 \u2014 External AP / Guest Network'
          : 'Step 4 \u2014 Wi-Fi Configuration';
      }
      if (sub) {
        sub.textContent = on
          ? 'Confirm bridge-only guest networking. Register the LAN access point after install.'
          : 'Choose an existing SSID on your MikroTik or create a dedicated Piso Wi-Fi network.';
      }
      var notice = document.getElementById('wifiNetworksNotice');
      if (on && notice) {
        notice.textContent = 'No MikroTik wireless detected. External AP mode is required.';
      }
      var c = selectedExistingCandidate || {};
      var bridgeEl = document.getElementById('externalApBridgeLabel');
      var netEl = document.getElementById('externalApNetworkLabel');
      if (bridgeEl) bridgeEl.textContent = c.bridgeName || '\u2014';
      if (netEl) netEl.textContent = c.guestNetwork || c.gatewayCidr || '\u2014';
      updateWifiNextButtonState();
    }

    function isApplyInFlight() {
      return applyExistingNetworkInFlight;
    }

    var SCAN_PROGRESS_LABELS = {
      queued: 'Queued...',
      connecting: 'Connecting...',
      logging_in: 'Logging in...',
      reading_bridges: 'Reading Bridges...',
      reading_addresses: 'Reading Addresses...',
      reading_pools: 'Reading Pools...',
      reading_dhcp_servers: 'Reading DHCP Servers...',
      reading_dhcp_networks: 'Reading DHCP Networks...',
      reading_firewall: 'Reading Firewall...',
      reading_hotspot: 'Reading Hotspot...',
      analyzing: 'Analyzing Configuration...',
      done: 'Done.',
      running: 'Scanning...'
    };

    // Debug logging (requirement 7) is opt-in only: append ?debug=1 to the
    // setup URL, or run `localStorage.setItem('renzfi_debug','1')` in the
    // browser console. Production/field devices stay silent by default.
    var RENZFI_DEBUG = (function () {
      try {
        if (/(?:^|[?&])debug=1(?:&|$)/.test(window.location.search)) return true;
        return window.localStorage && window.localStorage.getItem('renzfi_debug') === '1';
      } catch (e) {
        return false;
      }
    })();

    function logExistingScanDebug(details) {
      if (!RENZFI_DEBUG) return;
      console.log('[existing-scan]', details);
    }

    function currentBootInstanceId() {
      return (setupStatus && setupStatus.bootInstanceId) || null;
    }

    function firewallStatusLabel(candidate) {
      if (!candidate) return 'Unknown';
      return candidate.apiAccessOk ? 'Allowed' : 'Needs repair';
    }

    function hotspotCheckLabel(candidate) {
      var compat = (candidate && candidate.compatibility) || {};
      if (compat.hotspot === 'pass') return 'Detected';
      if (compat.hotspot === 'unknown') return 'Not inspected';
      if (compat.hotspot === 'warning') return 'Needs review';
      return 'Not detected';
    }

    function renderCompatibleNetworkSummary(candidate) {
      var out = document.getElementById('existingScanResults');
      if (!candidate) {
        out.style.display = 'none';
        out.innerHTML = '';
        return;
      }
      out.style.display = 'block';
      out.innerHTML =
        '<ul class="scan-checklist">' +
        '<li>&#10003; Existing Network Detected</li>' +
        '<li>&#10003; Configuration Compatible</li>' +
        '<li>&#10003; Ready to Configure</li>' +
        '</ul>';
    }

    function scanProgressLabel(job) {
      if (!job) return 'Scanning...';
      if (job.progressLabel) return job.progressLabel;
      if (job.progressStage && SCAN_PROGRESS_LABELS[job.progressStage]) {
        return SCAN_PROGRESS_LABELS[job.progressStage];
      }
      if (job.progress && SCAN_PROGRESS_LABELS[job.progress]) {
        return SCAN_PROGRESS_LABELS[job.progress];
      }
      if (job.state === 'queued') return SCAN_PROGRESS_LABELS.queued;
      return SCAN_PROGRESS_LABELS.running;
    }

    function setScanProgressVisible(on, label) {
      var panel = document.getElementById('existingScanProgress');
      panel.style.display = on ? 'flex' : 'none';
      if (label) {
        document.getElementById('existingScanProgressLabel').textContent = label;
      }
      if (on) {
        showBusyOverlay('Router Scan', label || 'Scanning router configuration\u2026');
      } else if (!isApplyInFlight() && !wifiSaveInFlight && !routerSubmitting) {
        hideBusyOverlay();
      }
    }

    function formatConfidenceDisplay(confidence) {
      if (!confidence) return 'Unknown';
      return confidence.charAt(0).toUpperCase() + confidence.slice(1);
    }

    function countVisibleCandidates(data) {
      var list = (data && data.candidates) || [];
      var count = 0;
      for (var i = 0; i < list.length; i++) {
        if (list[i].status !== 'rejected_overlap') count++;
      }
      return count;
    }

    function formatScanStatusLabel(status) {
      if (!status) return 'Unknown';
      return String(status).replace(/_/g, ' ').replace(/\b\w/g, function (ch) {
        return ch.toUpperCase();
      });
    }

    // Rule #4 (Setup Simplification Pass): "Partial" must never be shown
    // when the scan actually succeeded and produced a network the user can
    // confirm — that is not an error condition. Only fall back to the raw
    // scan status label (which may legitimately read "Partial Only") when
    // there is nothing confirmable, i.e. a real timeout/permission/missing
    // table/command failure occurred.
    function computeDisplayStatus(data) {
      if (!data) return 'Unknown';
      if (data.confirmAllowed) {
        var top = confirmCandidateFromScan(data);
        return (top && top.status === 'compatible_candidate') ? 'Complete' : 'Compatible';
      }
      return formatScanStatusLabel(data.status || data.scanStatus);
    }

    function topScoringCandidate(data) {
      var list = (data && data.candidates) || [];
      var best = null;
      for (var i = 0; i < list.length; i++) {
        if (list[i].status === 'rejected_overlap') continue;
        if (!best || (list[i].adoptionScore || 0) > (best.adoptionScore || 0)) {
          best = list[i];
        }
      }
      return best;
    }

    function confirmCandidateFromScan(data) {
      if (!data || !data.confirmAllowed || data.expired) return null;
      if (data.confirmCandidateId && data.candidates) {
        for (var i = 0; i < data.candidates.length; i++) {
          if (data.candidates[i].id === data.confirmCandidateId) {
            return data.candidates[i];
          }
        }
      }
      return topScoringCandidate(data);
    }

    // Part 2 (UX Simplification Pass): the appliance shows a plain
    // Router IP / Bridge / Hotspot / Status checklist here — no scan
    // duration, table counts, raw percentages, or confidence scores.
    // Those engineering diagnostics remain available in the browser
    // console (RENZFI_DEBUG) for support use, but are not appliance UI.
    function bridgeCheckLabel(candidate) {
      var compat = (candidate && candidate.compatibility) || {};
      if (compat.bridge === 'pass') return 'Detected';
      if (compat.bridge === 'unknown') return 'Not inspected';
      if (compat.bridge === 'warning') return 'Needs review';
      return 'Not detected';
    }

    function renderScanSummary(data) {
      var box = document.getElementById('existingScanSummary');
      if (!data) {
        box.style.display = 'none';
        box.innerHTML = '';
        return;
      }
      var routerIp = data.router ||
        savedRouterHost ||
        document.getElementById('routerHost').value.trim() || 'Unknown';
      var top = topScoringCandidate(data);
      var statusLabel = computeDisplayStatus(data);
      box.style.display = 'block';
      box.innerHTML =
        '<strong>Router Scan</strong>' +
        '<div class="row"><span>Router IP</span><span>' + routerIp + '</span></div>' +
        '<div class="row"><span>Bridge</span><span>' + bridgeCheckLabel(top) + '</span></div>' +
        '<div class="row"><span>Hotspot</span><span>' + hotspotCheckLabel(top) + '</span></div>' +
        '<div class="row"><span>Status</span><span>' + statusLabel + '</span></div>';
    }

    function queueAutoExistingNetworkScan() {
      startExistingNetworkScan({ auto: true });
    }

    // Part 6 (Product Polish Pass): advancing past "Router connection saved"
    // is now automatic — no button. The delay is a plain client-side timer
    // (no network call gates it), so it can never hang even if the
    // subsequent scan is slow to start.
    var routerSaveAutoAdvanceTimer = null;

    function showRouterSaveSuccessModal() {
      document.getElementById('routerSaveSuccessModal').classList.remove('hidden-block');
      clearTimeout(routerSaveAutoAdvanceTimer);
      routerSaveAutoAdvanceTimer = setTimeout(function () {
        hideRouterSaveSuccessModal();
        proceedAfterRouterSave();
      }, 1400);
    }

    function hideRouterSaveSuccessModal() {
      clearTimeout(routerSaveAutoAdvanceTimer);
      routerSaveAutoAdvanceTimer = null;
      document.getElementById('routerSaveSuccessModal').classList.add('hidden-block');
    }

    function proceedAfterRouterSave() {
      // Always re-fetch status — stale setupStatus can still say wizardStep=complete
      // from a prior adoption and would skip Router Scan / Wi-Fi entirely.
      loadSetupStatus(function (json) {
        resumeWizardFromStatus(json, { beginExistingNetworkScan: true });
      }, function () {
        resumeWizardFromStatus({ data: setupStatus || {} }, { beginExistingNetworkScan: true });
      });
    }

    function startExistingNetworkScan(options) {
      options = options || {};
      if (!setupWizardEnabled()) return;
      if (scanSubmitting || activeExistingScanJobId || isApplyInFlight()) return;
      scanSubmitting = true;
      setCurrentJobState('scan-starting');
      setCurrentJobType('existing-network-scan');
      existingScanRestartFlagged = false;
      showFormError('existingScanError', '');
      updateExistingScanButtonState();
      setScanProgressVisible(true, SCAN_PROGRESS_LABELS.connecting);
      document.getElementById('existingScanStatus').textContent =
        options.auto
          ? 'Automatically scanning existing router configuration… No router changes will be made.'
          : 'Scanning existing router configuration… No router changes will be made.';
      // Rule #1 (Setup Simplification Pass): only the explicit Rescan button
      // (auto === false) may force a new live RouterOS scan. Automatic
      // triggers reuse the server-side cached result if one already exists.
      var forceRescan = options.auto === false;
      fetch('/api/setup/router/existing-network/scan', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ rescan: forceRescan })
      })
        .then(function (r) { return r.json().then(function (j) { return { status: r.status, json: j }; }); })
        .then(function (res) {
          if (res.json && res.json.success && res.json.data) {
            if (res.json.data.jobId) {
              activeExistingScanJobId = res.json.data.jobId;
              setCurrentJobState('scan-queued');
              pollExistingNetworkJob(activeExistingScanJobId, handleExistingScanJobDone, function (job) {
                var jobState = typeof job === 'string' ? job : (job && job.state);
                if (jobState) {
                  setCurrentJobState('scan-' + jobState);
                }
                var label = typeof job === 'string' ? scanProgressLabel({ state: job }) : scanProgressLabel(job);
                setScanProgressVisible(true, label);
                document.getElementById('existingScanStatus').textContent =
                  label + ' No router changes will be made.';
              });
              return;
            }
            handleExistingScanJobDone({ status: res.status, json: res.json });
            return;
          }
          scanSubmitting = false;
          activeExistingScanJobId = null;
          setCurrentJobState('idle');
          setScanProgressVisible(false);
          updateExistingScanButtonState();
          showFormError('existingScanError', (res.json && res.json.error) || 'Unable to start scan');
          document.getElementById('existingScanStatus').textContent =
            'Scan required — save the router connection to scan automatically.';
        })
        .catch(function () {
          scanSubmitting = false;
          activeExistingScanJobId = null;
          setCurrentJobState('idle');
          setScanProgressVisible(false);
          updateExistingScanButtonState();
          showFormError('existingScanError', 'Unable to start scan');
          document.getElementById('existingScanStatus').textContent =
            'Scan required — save the router connection to scan automatically.';
        });
    }

    function updateExistingScanButtonState() {
      var busy = scanSubmitting || !!activeExistingScanJobId;
      document.getElementById('scanExistingBtn').disabled =
        busy || isApplyInFlight();
    }

    var busyOverlayPulseTimer = null;
    var busyOverlayPulsePct = 12;

    function stopBusyOverlayPulse() {
      if (busyOverlayPulseTimer) {
        clearInterval(busyOverlayPulseTimer);
        busyOverlayPulseTimer = null;
      }
    }

    function startBusyOverlayPulse() {
      stopBusyOverlayPulse();
      busyOverlayPulsePct = 12;
      var bar = document.getElementById('busyProgressBar');
      if (!bar) return;
      bar.classList.remove('indeterminate');
      bar.style.width = busyOverlayPulsePct + '%';
      busyOverlayPulseTimer = setInterval(function () {
        if (busyOverlayPulsePct < 88) {
          busyOverlayPulsePct += 2;
          bar.style.width = busyOverlayPulsePct + '%';
        }
      }, 700);
    }

    function showBusyOverlay(title, detail, options) {
      options = options || {};
      var overlay = document.getElementById('busyOverlay');
      var card = document.getElementById('busyCard');
      var bar = document.getElementById('busyProgressBar');
      var actions = document.getElementById('busyActions');
      var hint = document.getElementById('busyHint');
      if (!overlay) return;
      card.classList.remove('busy-error');
      document.getElementById('busyTitle').textContent = title || 'Working\u2026';
      document.getElementById('busyDetail').textContent =
        detail || 'Please wait. This can take a moment.';
      actions.classList.add('hidden-block');
      actions.innerHTML = '';
      if (hint) {
        hint.style.display = options.hideHint ? 'none' : 'block';
        if (options.hint) hint.textContent = options.hint;
      }
      if (typeof options.progress === 'number') {
        stopBusyOverlayPulse();
        bar.classList.remove('indeterminate');
        bar.style.width = Math.max(0, Math.min(100, options.progress)) + '%';
      } else if (options.indeterminate) {
        stopBusyOverlayPulse();
        bar.classList.add('indeterminate');
        bar.style.width = '';
      } else {
        startBusyOverlayPulse();
      }
      overlay.classList.remove('hidden-block');
    }

    function updateBusyOverlay(detail, progress) {
      var detailEl = document.getElementById('busyDetail');
      if (detailEl && detail) detailEl.textContent = detail;
      if (typeof progress === 'number') {
        stopBusyOverlayPulse();
        var bar = document.getElementById('busyProgressBar');
        if (bar) {
          bar.classList.remove('indeterminate');
          bar.style.width = Math.max(0, Math.min(100, progress)) + '%';
        }
      }
    }

    function hideBusyOverlay() {
      stopBusyOverlayPulse();
      var overlay = document.getElementById('busyOverlay');
      var actions = document.getElementById('busyActions');
      if (actions) {
        actions.classList.add('hidden-block');
        actions.innerHTML = '';
      }
      if (overlay) overlay.classList.add('hidden-block');
    }

    function hideAdoptProgressModal() {
      hideBusyOverlay();
    }

    function showAdoptProgressModal(label) {
      showBusyOverlay('Applying Configuration', label || 'Applying configuration on your router\u2026');
    }

    function showAdoptFailureModal(message) {
      stopBusyOverlayPulse();
      var overlay = document.getElementById('busyOverlay');
      var card = document.getElementById('busyCard');
      var actions = document.getElementById('busyActions');
      card.classList.add('busy-error');
      document.getElementById('busyTitle').textContent = 'Configuration failed';
      document.getElementById('busyDetail').innerHTML =
        'RouterOS returned:<br><br><code style="word-break:break-all;display:block;text-align:left">' +
        escapeHtml(message || 'Unknown error') + '</code>';
      actions.classList.remove('hidden-block');
      actions.innerHTML =
        '<button id="adoptRetryBtn" type="button">Retry</button>' +
        '<button id="adoptBackBtn" type="button" class="secondary">Back</button>';
      overlay.classList.remove('hidden-block');
      document.getElementById('adoptRetryBtn').addEventListener('click', function () {
        hideBusyOverlay();
        executeAdoption();
      });
      document.getElementById('adoptBackBtn').addEventListener('click', function () {
        hideBusyOverlay();
        showPanel('panelWifi');
      });
    }

    function restoreExistingScanUi() {
      if (existingScanRestartFlagged) return;
      if (lastExistingScanData) {
        renderExistingScanResults(lastExistingScanData);
      }
    }

    function scanConfirmEnabled(data) {
      if (!data || data.invalidatedReason) return false;
      return !!(data.confirmAllowed && !data.expired);
    }

    function updateAdoptButtonState() {
      var confirmEnabled = scanConfirmEnabled(lastExistingScanData);
      var confirmButton = document.getElementById('adoptExistingBtn');
      var applyBusy = isApplyInFlight();
      var buttonDisabled =
        !confirmEnabled || scanSubmitting || applyBusy;
      confirmButton.disabled = buttonDisabled;
    }

    function scanBlockingSummary(data) {
      var top = topScoringCandidate(data);
      if (!top) return 'No router network candidate was found.';
      var reasons = [];
      var compat = top.compatibility || {};
      if (compat.bridge !== 'pass') reasons.push('Bridge not detected');
      if (compat.gateway !== 'pass') reasons.push('Gateway not found on bridge');
      if (compat.hotspot !== 'pass') reasons.push('Hotspot not detected');
      if (top.status === 'rejected_overlap') {
        reasons.push('Network overlaps the appliance subnet');
      }
      if (top.confidenceReasons && top.confidenceReasons.length) {
        for (var ri = 0; ri < top.confidenceReasons.length; ri++) {
          reasons.push(String(top.confidenceReasons[ri]).replace(/_/g, ' '));
        }
      }
      if (!reasons.length) {
        return 'Review router settings and rescan.';
      }
      var seen = {};
      return reasons.filter(function (item) {
        if (seen[item]) return false;
        seen[item] = true;
        return true;
      }).join('; ');
    }

    function renderExistingScanResults(data) {
      updateExistingScanState("renderExistingScanResults", data);
      data = lastExistingScanData || data;
      showFormError('existingScanError', '');
      setScanProgressVisible(false);
      renderScanSummary(data);

      var status = document.getElementById('existingScanStatus');
      var statusLabel = computeDisplayStatus(data);
      selectedExistingCandidate = confirmCandidateFromScan(data);
      var invalidateReason = data.invalidatedReason ||
        (setupStatus && setupStatus.networkProvisioning &&
         setupStatus.networkProvisioning.invalidatedReason) || '';
      if (data.expired || invalidateReason) {
        selectedExistingCandidate = null;
        status.textContent =
          'Scan unavailable — no router changes were made. Status: ' + statusLabel + '.';
        renderCompatibleNetworkSummary(null);
        var expiredNotice = document.createElement('div');
        expiredNotice.className = 'notice';
        expiredNotice.textContent = invalidateReason
          ? ('Previous scan invalidated (' + invalidateReason + '). Please scan again.')
          : 'The router scan has expired. Please scan again.';
        document.getElementById('existingScanResults').appendChild(expiredNotice);
      } else if (scanConfirmEnabled(data) && selectedExistingCandidate) {
        status.textContent =
          'Network ready to confirm. Status: ' + statusLabel +
          '. No router changes were made.';
        renderCompatibleNetworkSummary(selectedExistingCandidate);
      } else if ((data.status || data.scanStatus) === 'no_compatible_candidate') {
        selectedExistingCandidate = null;
        status.textContent =
          'Scan complete — no compatible network found. Check router DHCP and pool settings, then rescan.';
        renderCompatibleNetworkSummary(null);
        var warn = document.createElement('div');
        warn.className = 'notice';
        warn.textContent = 'No compatible guest network candidate was detected.';
        document.getElementById('existingScanResults').appendChild(warn);
      } else {
        selectedExistingCandidate = null;
        status.textContent =
          'Scan complete — no router changes were made. Status: ' + statusLabel + '.';
        renderCompatibleNetworkSummary(null);
        if (!data.confirmAllowed) {
          var partial = document.createElement('div');
          partial.className = 'notice';
          partial.textContent =
            'Cannot confirm yet: ' + scanBlockingSummary(data);
          document.getElementById('existingScanResults').appendChild(partial);
        }
      }
      updateAdoptButtonState();
    }

    // Confirms (or rules out) a real device restart before ever surfacing
    // DEVICE_RESTARTED to the user (requirement 4). A missing/expired job
    // id is NOT sufficient evidence on its own — job-table races (TTL /
    // single-slot worker queue eviction), a slow mobile reconnect, or a
    // dropped request can all make a completed job "disappear" from the
    // worker without the ESP32 ever rebooting. The only accepted proof is
    // a changed bootInstanceId read fresh from /api/setup/status.
    function confirmExistingScanRestart(jobId, bootInstanceBefore, onDone) {
      fetch('/api/setup/status', { cache: 'no-store' })
        .then(function (r) { return r.json(); })
        .then(function (json) {
          var data = (json && json.data) || {};
          setupStatus = data;
          var bootInstanceAfter = data.bootInstanceId || null;
          var restartDetected = !!bootInstanceBefore && !!bootInstanceAfter &&
            bootInstanceBefore !== bootInstanceAfter;
          logExistingScanDebug({
            jobId: jobId,
            httpStatus: 404,
            jobState: 'not_found',
            resultSuccess: false,
            hasScanData: false,
            restartDetected: restartDetected,
            bootInstanceBefore: bootInstanceBefore,
            bootInstanceAfter: bootInstanceAfter
          });
          if (restartDetected) {
            existingScanRestartFlagged = true;
            updateExistingScanState("confirmExistingScanRestart", null);
            onDone({
              status: 404,
              json: {
                success: false,
                error: 'Device restarted during scan. Reconnect and scan again.',
                code: 'DEVICE_RESTARTED'
              }
            });
            return;
          }
          // Job id vanished but the boot instance is unchanged — the ESP32
          // never restarted. Do not claim it did; surface a neutral,
          // retryable error and leave any previously cached scan alone.
          onDone({
            status: 404,
            json: {
              success: false,
              error: 'Scan result unavailable. Please scan again.',
              code: 'EXISTING_NETWORK_SCAN_JOB_LOST'
            }
          });
        })
        .catch(function () {
          logExistingScanDebug({
            jobId: jobId,
            httpStatus: 404,
            jobState: 'not_found',
            resultSuccess: false,
            hasScanData: false,
            restartDetected: false,
            bootInstanceBefore: bootInstanceBefore,
            bootInstanceAfter: null
          });
          onDone({
            status: 0,
            json: {
              success: false,
              error: 'Scan result unavailable. Please scan again.',
              code: 'EXISTING_NETWORK_SCAN_JOB_LOST'
            }
          });
        });
    }

    function pollExistingNetworkJob(jobId, onDone, onProgress, activityGeneration) {
      if (activityGeneration == null) activityGeneration = scanActivityGeneration;
      var attempts = 0;
      var pollErrorCount = 0;
      var maxPollErrorRetries = 3;
      var bootInstanceBefore = currentBootInstanceId();

      function tick() {
        if (activityGeneration !== scanActivityGeneration || setupCompleteShown) return;
        attempts++;
        fetch('/api/setup/router/existing-network/jobs/' + jobId, { cache: 'no-store' })
          .then(function (r) {
            return r.json().then(function (j) { return { status: r.status, json: j }; });
          })
          .then(function (resp) {
            pollErrorCount = 0;

            if (resp.status === 404 &&
                resp.json && resp.json.code === 'JOB_NOT_FOUND') {
              confirmExistingScanRestart(jobId, bootInstanceBefore, onDone);
              return;
            }

            // Unwrap exactly as: job = response.data; result = job.result;
            // scan = result.data. (requirement 2)
            // Completed/failed scan jobs may also return the worker payload
            // directly (no job-status wrapper) to avoid double serialization.
            var response = resp.json || {};
            var job = response.data || {};
            var result = job.result;

            if (!job.state && !job.result) {
              if (response.success === true &&
                  (job.scanStatus || job.status || job.schemaVersion ||
                   job.confirmAllowed !== undefined || job.candidates)) {
                setCurrentJobState('scan-completed');
                onDone({ status: resp.status || 200, json: response });
                return;
              }
              if (response.success === false &&
                  (response.code || response.error)) {
                setCurrentJobState('scan-failed');
                onDone({ status: resp.status || 500, json: response });
                return;
              }
            }

            logExistingScanDebug({
              jobId: jobId,
              httpStatus: resp.status,
              jobState: job.state,
              resultSuccess: result ? result.success : undefined,
              hasScanData: !!(result && result.data),
              restartDetected: false,
              bootInstanceBefore: bootInstanceBefore,
              bootInstanceAfter: currentBootInstanceId()
            });

            if (job.state === 'queued' || job.state === 'running') {
              if (onProgress) onProgress(job);
              if (attempts < 120) setTimeout(tick, 500);
              else {
                onDone({
                  status: 504,
                  json: { success: false, error: 'Scan timed out', code: 'ROUTER_JOB_TIMEOUT' }
                });
              }
              return;
            }

            // Requirement 1/3: a completed+successful job is authoritative
            // and is checked BEFORE any failure/restart branch below.
            if (response.success === true && job.state === 'completed' &&
                result && result.success === true && result.data) {
              setCurrentJobState('scan-completed');
              onDone({ status: job.httpStatus || resp.status || 200, json: result });
              return;
            }

            if (job.state === 'failed') {
              setCurrentJobState('scan-failed');
              onDone({
                status: job.httpStatus || 500,
                json: result || {
                  success: false,
                  error: response.error || 'Existing network scan failed',
                  code: job.code || 'EXISTING_NETWORK_SCAN_FAILED'
                }
              });
              return;
            }
            if (result) {
              onDone({ status: job.httpStatus || 200, json: result });
              return;
            }
            onDone({
              status: 500,
              json: response.success === false ? response : {
                success: false,
                error: 'Invalid existing network scan job response',
                code: 'INVALID_JOB_RESPONSE'
              }
            });
          })
          .catch(function () {
            // Transient network/parse error (requirement 5): preserve the
            // active job id and retry with backoff before giving up. Never
            // treat this as evidence of a restart.
            pollErrorCount++;
            logExistingScanDebug({
              jobId: jobId,
              httpStatus: 0,
              jobState: 'poll_error',
              resultSuccess: false,
              hasScanData: false,
              restartDetected: false,
              bootInstanceBefore: bootInstanceBefore,
              bootInstanceAfter: null
            });
            if (pollErrorCount <= maxPollErrorRetries) {
              setTimeout(tick, 500 * pollErrorCount);
              return;
            }
            onDone({
              status: 0,
              json: {
                success: false,
                error: 'Scan poll failed',
                code: 'EXISTING_NETWORK_SCAN_POLL_FAILED'
              }
            });
          });
      }
      tick();
    }

    function handleExistingScanJobDone(finalRes) {
      scanSubmitting = false;
      activeExistingScanJobId = null;
      setCurrentJobState('scan-complete');
      updateExistingScanButtonState();
      setScanProgressVisible(false);

      // Requirement 3/6: a successfully completed job always wins, even
      // over a restart flag left over from an earlier ambiguous poll.
      if (finalRes.json && finalRes.json.success && finalRes.json.data) {
        existingScanRestartFlagged = false;
        showFormError('existingScanError', '');
        renderExistingScanResults(finalRes.json.data);
        return;
      }

      var errJson = finalRes.json || {};
      var msg = errJson.error || errJson.message || 'Existing network scan failed';
      var code = errJson.code || '';
      showFormError('existingScanError', code ? (code + ': ' + msg) : msg);
      document.getElementById('existingScanStatus').textContent =
        'Scan failed — no router changes were made.';
    }

    // Setup Simplification Pass (UX reorder): visual order is now
    // Owner -> Router Connection -> Router Scan -> Wi-Fi Configuration ->
    // Complete. The backend's wizardStep values (see
    // SetupProvisioningManager::wizardStepForPhase) are unchanged; only the
    // panel each step maps to has moved, handled explicitly in
    // resumeWizardFromStatus() below.
    var PANEL_ORDER = [
      'panelOwner', 'panelMikrotik', 'panelReview', 'panelWifi', 'panelProvisioned'
    ];

    function setupWizardEnabled() {
      return !setupStatus || setupStatus.setupWizardEnabled !== false;
    }

    function formatSessionRemaining(ms) {
      var totalSec = Math.max(0, Math.ceil(ms / 1000));
      var mins = Math.floor(totalSec / 60);
      var secs = totalSec % 60;
      return mins + ':' + (secs < 10 ? '0' : '') + secs;
    }

    function redirectToLockedSetup() {
      if (setupSessionLocking) return;
      setupSessionLocking = true;
      window.location.replace('/admin/setup');
    }

    function updateSetupSessionUi() {
      var bar = document.getElementById('setupSessionBar');
      var label = document.getElementById('setupSessionLabel');
      if (!bar || !label) return;
      var remaining = setupSessionEndsAt > 0 ? (setupSessionEndsAt - Date.now()) : 0;
      var reentry = !!(setupStatus && setupStatus.setupUnlockRequired);
      if (!reentry || remaining <= 0) {
        bar.classList.remove('visible');
        if (reentry && setupSessionEndsAt > 0) {
          redirectToLockedSetup();
        }
        return;
      }
      bar.classList.add('visible');
      label.textContent =
        'Setup unlocked for ' + formatSessionRemaining(remaining) +
        ' — locks automatically when time runs out, or when you Finish or Cancel.';
    }

    function syncSetupUnlockSession(data) {
      data = data || setupStatus || {};
      var remaining = Number(data.setupUnlockSessionRemainingMs || 0);
      if (data.setupUnlockRequired && remaining > 0) {
        setupSessionEndsAt = Date.now() + remaining;
        if (!setupSessionTimerId) {
          setupSessionTimerId = setInterval(updateSetupSessionUi, 1000);
        }
      } else {
        setupSessionEndsAt = 0;
        if (setupSessionTimerId) {
          clearInterval(setupSessionTimerId);
          setupSessionTimerId = null;
        }
      }
      updateSetupSessionUi();
    }

    function cancelUnlockedSetup() {
      if (setupSessionLocking) return;
      setupSessionLocking = true;
      var btn = document.getElementById('cancelSetupBtn');
      if (btn) btn.disabled = true;
      fetch('/api/setup/lock', { method: 'POST' })
        .then(function () { window.location.replace('/admin/setup'); })
        .catch(function () { window.location.replace('/admin/setup'); });
    }

    function showProductionHandoffNotice() {
      var notice = document.getElementById('productionReadyNotice');
      notice.classList.remove('hidden-block');
      document.getElementById('operatorOptionalSection').classList.add('hidden-block');
      notice.innerHTML =
        '<strong>Setup completed successfully.</strong><br><br>' +
        'Connect to the MikroTik LAN to open the Admin Dashboard.';
    }

    function showDashboardFinalizeNotice() {
      var notice = document.getElementById('productionReadyNotice');
      notice.classList.remove('hidden-block');
      notice.innerHTML =
        '<div style="display:flex;align-items:flex-start;gap:12px">' +
        '<div class="scan-progress-spinner" style="flex-shrink:0;margin-top:2px" aria-hidden="true"></div>' +
        '<div>' +
        '<strong>Finalizing setup...</strong><br>' +
        'Preparing the Admin Dashboard...<br>' +
        '<span class="field-hint">This usually takes a few seconds.</span>' +
        '</div></div>';
      document.getElementById('operatorOptionalSection').classList.add('hidden-block');
    }

    function showProductionHandoffTimeoutNotice(adminUrl) {
      var notice = document.getElementById('productionReadyNotice');
      notice.classList.remove('hidden-block');
      document.getElementById('operatorOptionalSection').classList.add('hidden-block');
      if (adminUrl) {
        notice.innerHTML =
          '<strong>Setup completed successfully.</strong><br><br>' +
          'The Admin Dashboard is taking longer than expected.<br><br>' +
          'Open:<br>' +
          '<a href="' + adminUrl + '" style="color:#bfdbfe">' + adminUrl + '</a><br><br>' +
          'or wait a few moments and refresh.';
      } else {
        showProductionHandoffNotice();
      }
    }

    function fetchHandoffHealth() {
      return fetch('/api/health', {
        method: 'GET',
        cache: 'no-store',
        headers: { 'Accept': 'application/json' }
      })
        .then(function (response) {
          if (!response.ok) return null;
          return response.json();
        })
        .catch(function () { return null; });
    }

    function waitForDashboardHandoff(options) {
      options = options || {};
      var timeoutMs = options.timeoutMs || 5000;
      var intervalMs = options.intervalMs || 500;
      var started = Date.now();
      var lastAdminUrl = null;

      function tick() {
        return fetchHandoffHealth().then(function (json) {
          if (json && json.success) {
            var data = json.data || {};
            if (data.adminUrl) lastAdminUrl = data.adminUrl;
            if (data.ready && data.adminUrl) {
              return { ready: true, adminUrl: data.adminUrl };
            }
          }
          if (Date.now() - started >= timeoutMs) {
            return { ready: false, adminUrl: lastAdminUrl };
          }
          return new Promise(function (resolve) {
            setTimeout(function () { resolve(tick()); }, intervalMs);
          });
        });
      }

      return tick();
    }

    function handoffToAdminDashboard(status) {
      if (dashboardRedirectStarted) return;
      dashboardRedirectStarted = true;
      showPanel('panelProvisioned');
      updateCompletePanelUi();
      refreshStepBars();
      showDashboardFinalizeNotice();
      waitForDashboardHandoff({ timeoutMs: 5000, intervalMs: 500 })
        .then(function (result) {
          if (result.ready && result.adminUrl) {
            window.location.replace(result.adminUrl);
            return;
          }
          showProductionHandoffTimeoutNotice(result.adminUrl);
        });
    }

    function updateSetupPhaseBanner() {}

    function updateCompletePanelUi() {
      var production = !!(setupStatus && setupStatus.productionMode);
      var wizard = (setupStatus && setupStatus.wizard) || {};
      var operatorDone = !!wizard.operatorConfigured;
      document.getElementById('operatorOptionalSection').classList.toggle(
        'hidden-block', production || operatorDone);
      var backBtn = document.getElementById('provisionedBackBtn');
      if (backBtn) {
        backBtn.classList.toggle('hidden-block', production || finishSetupSubmitting);
      }
      if (!production && !finishSetupSubmitting) {
        document.getElementById('panelProvisionedTitle').textContent =
          'Step 5 \u2014 Administrator & Operator';
        document.getElementById('panelProvisionedSub').classList.remove('hidden-block');
        document.getElementById('panelProvisionedSub').textContent =
          'Your administrator account is ready. Optionally create an operator account for staff use.';
        document.getElementById('productionReadyNotice').classList.add('hidden-block');
      }
    }

    function enterProductionMode(status) {
      if (dashboardRedirectStarted) return false;
      status = status || setupStatus || {};
      setupStatus = status;
      applyExistingNetworkInFlight = false;
      scanSubmitting = false;
      activeExistingScanJobId = null;
      handoffToAdminDashboard(status);
      return false;
    }

    function showSetupCompletePanel(statusJson) {
      applyExistingNetworkInFlight = false;
      showBusyOverlay('Configuration Applied', 'Moving to the next step\u2026', { progress: 100 });
      setTimeout(function () {
        hideBusyOverlay();
        // Prefer a fresh status read so Step 5 only opens when backend
        // lifecycle (including Wi-Fi complete) actually allows it.
        loadSetupStatus(function (json) {
          resumeWizardFromStatus(json);
        }, function () {
          if (statusJson) resumeWizardFromStatus(statusJson);
        });
      }, 800);
    }

    // Part 3/12 (Product Polish Pass): appliance-friendly labels for the
    // same backend finish stages (persist-local/portal-verify/walled-garden)
    // that were already reported by RouterProvisioningWorker's finish
    // pipeline — no new stages, no new RouterOS calls, wording only.
    var FINISH_PROGRESS_LABELS = {
      queued: 'Applying Configuration\u2026',
      running: 'Applying Configuration\u2026',
      'persist-local': 'Saving Settings\u2026',
      'portal-verify': 'Checking Captive Portal\u2026',
      'walled-garden': 'Restarting Services\u2026'
    };

    // Part 6: "Creating Accounts" already happened (Operator was created or
    // explicitly skipped) before finishSetup() is ever called, so it is
    // shown as done from the start — this is not a fabricated checkmark.
    function setFinishProgressLabel(text) {
      var msg = text || 'Applying Configuration\u2026';
      var el = document.getElementById('finishActiveStepLabel');
      if (el) el.textContent = msg;
      var fallback = document.getElementById('globalLoading');
      if (fallback) fallback.textContent = msg;
      updateBusyOverlay(msg);
    }

    function showFinishingUi() {
      document.getElementById('operatorOptionalSection').classList.add('hidden-block');
      var notice = document.getElementById('productionReadyNotice');
      notice.classList.remove('hidden-block');
      notice.innerHTML =
        '<strong>Setting up your appliance&hellip;</strong>' +
        '<ul class="scan-checklist" style="margin:10px 0 0">' +
        '<li>&#10003; Creating Accounts</li>' +
        '<li style="display:flex;align-items:center;gap:8px">' +
        '<span class="scan-progress-spinner" style="flex-shrink:0" aria-hidden="true"></span>' +
        '<span id="finishActiveStepLabel">Applying Configuration&hellip;</span>' +
        '</li></ul>';
      showBusyOverlay('Finishing Setup', 'Applying Configuration\u2026');
    }

    function escapeHtml(text) {
      return String(text || '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
    }

    function resolveDashboardUrl(data) {
      data = data || setupStatus || {};
      if (data.adminUrl) return data.adminUrl;
      var eth = data.ethernet || {};
      if (eth.ip) return 'http://' + eth.ip + '/login';
      return '';
    }

    function formatFirmwareVersion(raw) {
      var v = String(raw || '').trim();
      if (!v) return '\u2014';
      if (v.charAt(0).toLowerCase() === 'v') return v;
      return 'v' + v;
    }

    function buildProductionInstallationSummaryHtml(data, healthJson) {
      data = data || {};
      var np = data.networkProvisioning || {};
      var routerHealth =
        (healthJson && healthJson.success && healthJson.data && healthJson.data.router) || {};
      var product = routerHealth.product || {};
      var routerModel = product.name || routerHealth.identity || 'MikroTik Router';
      var routerIp = savedRouterHost ||
        (document.getElementById('routerHost') &&
          document.getElementById('routerHost').value.trim()) ||
        '\u2014';
      var productionSsid = np.externalApOnly
        ? 'External AP (register after install)'
        : (np.selectedSsidHint || wifiSelection.selectedSsid ||
          wifiSelection.ssid || '\u2014');
      var hotspotStatus = np.hotspotDetected ? 'Active' : 'Configured';
      var firmwareLabel = formatFirmwareVersion(data.firmwareVersion);
      var installStateRaw = String(data.installationState || 'provisioned');
      var installState = installStateRaw.charAt(0).toUpperCase() + installStateRaw.slice(1);
      var portalStatusRaw = String(data.portalStatus || '').toLowerCase();
      var portalLabel = 'Manual (MikroTik)';
      if (portalStatusRaw === 'verified') portalLabel = 'Verified on MikroTik';
      else if (portalStatusRaw === 'skipped') portalLabel = 'Skipped (manual deploy)';
      else if (portalStatusRaw === 'unverified') portalLabel = 'Unverified (manual deploy)';
      var adminHint = 'http://10.10.10.2/login';

      return (
        '<div class="install-summary">' +
        '<div class="install-divider"></div>' +
        '<div class="install-summary-heading">Installation Complete</div>' +
        '<div class="install-divider"></div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Router Model</div>' +
        '<div class="install-section-value">' + escapeHtml(routerModel) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Router IP</div>' +
        '<div class="install-section-value">' + escapeHtml(routerIp) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Production SSID</div>' +
        '<div class="install-section-value">' + escapeHtml(productionSsid) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Hotspot Status</div>' +
        '<div class="install-section-ok">&#10003; ' + escapeHtml(hotspotStatus) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Captive Portal</div>' +
        '<div class="install-section-value">' + escapeHtml(portalLabel) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Firmware Version</div>' +
        '<div class="install-section-value">' + escapeHtml(firmwareLabel) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Installation State</div>' +
        '<div class="install-section-value">' + escapeHtml(installState) + '</div>' +
        '</div>' +
        '<div class="install-divider"></div>' +
        '<div class="install-important">' +
        '<strong>Installation complete.</strong> Connect to the management network, then open ' +
        escapeHtml(adminHint) +
        '</div>' +
        '<div class="install-divider"></div>' +
        '<div class="install-actions">' +
        '<button id="acknowledgeSetupCompleteBtn" type="button">Finish</button>' +
        '</div>' +
        '</div>'
      );
    }

    function buildInstallationSummaryHtml(data, adminUrl) {
      data = data || setupStatus || {};
      var wizard = data.wizard || {};
      var np = data.networkProvisioning || {};
      var ownerName = data.ownerDisplayName ||
        document.getElementById('ownerDisplayName').value.trim() || '\u2014';
      var routerIp = savedRouterHost ||
        document.getElementById('routerHost').value.trim() || '\u2014';
      var wifiMode = np.wifiMode || wifiSelection.mode || 'existing';
      var wifiSsid = np.selectedSsidHint || wifiSelection.selectedSsid ||
        wifiSelection.ssid || '';
      var wifiModeLabel = np.externalApOnly || wifiMode === 'external_ap'
        ? 'External AP / Bridge-only'
        : (wifiMode === 'new' ? 'Create New SSID' : 'Using Existing SSID');
      var firmwareLabel = formatFirmwareVersion(data.firmwareVersion);
      var dashboardUrl = adminUrl || resolveDashboardUrl(data) || '\u2014';
      var operatorSection = wizard.operatorConfigured
        ? '<div class="install-section-ok">&#10003; Created</div>'
        : '<div class="install-section-detail">Skipped for now</div>';

      return (
        '<div class="install-summary">' +
        '<div class="install-divider"></div>' +
        '<div class="install-summary-heading">Installation Summary</div>' +
        '<div class="install-divider"></div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Owner</div>' +
        '<div class="install-section-value">' + escapeHtml(ownerName) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Router</div>' +
        '<div class="install-section-ok">&#10003; Connected</div>' +
        '<div class="install-section-detail">' + escapeHtml(routerIp) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Wi-Fi</div>' +
        '<div class="install-section-value">' + escapeHtml(wifiModeLabel) + '</div>' +
        (wifiSsid ? '<div class="install-section-detail">' + escapeHtml(wifiSsid) + '</div>' : '') +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Administrator</div>' +
        '<div class="install-section-ok">&#10003; Created</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Operator</div>' +
        operatorSection +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Captive Portal</div>' +
        (function () {
          var ps = String(data.portalStatus || '').toLowerCase();
          if (ps === 'verified') {
            return '<div class="install-section-ok">&#10003; Verified on MikroTik</div>';
          }
          if (ps === 'skipped') {
            return '<div class="install-section-detail">Skipped (manual deploy)</div>';
          }
          if (ps === 'unverified') {
            return '<div class="install-section-detail">Unverified (manual deploy)</div>';
          }
          return '<div class="install-section-detail">Manual (MikroTik)</div>';
        })() +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Firmware</div>' +
        '<div class="install-section-value">' + escapeHtml(firmwareLabel) + '</div>' +
        '</div>' +
        '<div class="install-section">' +
        '<div class="install-section-label">Dashboard</div>' +
        '<div class="dashboard-address" id="dashboardAddressText">' + escapeHtml(dashboardUrl) + '</div>' +
        '</div>' +
        '<div class="install-divider"></div>' +
        '<div class="install-important">' +
        '<strong>Important</strong>' +
        'Use the Management Wi-Fi only during setup. Customers should connect to the Piso Wi-Fi network.' +
        '</div>' +
        '<div class="install-divider"></div>' +
        '<div class="install-actions">' +
        '<button id="copyDashboardAddressBtn" type="button" class="secondary">Copy Address</button>' +
        '<button id="goToDashboardBtn" type="button">Go to Dashboard</button>' +
        '</div>' +
        '</div>'
      );
    }

    function copyDashboardAddress(address) {
      if (!address || address === '\u2014') return;
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(address).catch(function () {});
        return;
      }
      var ta = document.createElement('textarea');
      ta.value = address;
      ta.style.position = 'fixed';
      ta.style.left = '-9999px';
      document.body.appendChild(ta);
      ta.select();
      try { document.execCommand('copy'); } catch (e) {}
      document.body.removeChild(ta);
    }

    function showFinalizingInstallationUi() {
      var notice = document.getElementById('productionReadyNotice');
      notice.classList.remove('hidden-block');
      notice.innerHTML =
        '<strong>Finalizing installation&hellip;</strong>' +
        '<div style="margin-top:10px;display:flex;align-items:center;gap:8px">' +
        '<span class="scan-progress-spinner" style="flex-shrink:0" aria-hidden="true"></span>' +
        '<span>Please wait while the appliance restarts.</span></div>';
    }

    function acknowledgeSetupComplete() {
      if (setupCompleteAckSubmitting) return;
      setupCompleteAckSubmitting = true;
      showFinalizingInstallationUi();
      fetch('/api/setup/complete', { method: 'POST' })
        .then(function (r) {
          return r.json().then(function (j) { return { status: r.status, json: j }; });
        })
        .then(function (res) {
          if (res.json && res.json.success) {
            return;
          }
          setupCompleteAckSubmitting = false;
          showSetupCompleteUi(setupStatus);
          showFormError('operatorFormError',
            (res.json && res.json.error) || 'Unable to finalize installation.');
        })
        .catch(function () {
          // Connection loss is expected once the appliance reboots.
        });
    }

    function showSetupCompleteUi(data) {
      data = data || setupStatus || {};
      setupStatus = data;
      showPanel('panelProvisioned');
      document.getElementById('panelProvisionedTitle').textContent = 'Installation Summary';
      document.getElementById('panelProvisionedSub').classList.add('hidden-block');
      document.getElementById('setupCompleteChecklist').classList.add('hidden-block');
      document.getElementById('operatorOptionalSection').classList.add('hidden-block');
      var backBtn = document.getElementById('provisionedBackBtn');
      if (backBtn) backBtn.classList.add('hidden-block');
      refreshStepBars();

      function renderSummary(healthJson) {
        var notice = document.getElementById('productionReadyNotice');
        notice.classList.remove('hidden-block');
        notice.innerHTML = buildProductionInstallationSummaryHtml(data, healthJson);
        var finishBtn = document.getElementById('acknowledgeSetupCompleteBtn');
        if (finishBtn) {
          finishBtn.addEventListener('click', acknowledgeSetupComplete);
        }
      }

      fetchHandoffHealth().then(renderSummary);
    }

    function finishProgressLabel(job) {
      if (!job) return FINISH_PROGRESS_LABELS.running;
      if (typeof job === 'string') {
        return FINISH_PROGRESS_LABELS[job] || FINISH_PROGRESS_LABELS.running;
      }
      if (job.stageLabel) return job.stageLabel;
      if (job.stage && FINISH_PROGRESS_LABELS[job.stage]) {
        return FINISH_PROGRESS_LABELS[job.stage];
      }
      return FINISH_PROGRESS_LABELS[job.state] || FINISH_PROGRESS_LABELS.running;
    }

    function setFinishButtonsDisabled(disabled) {
      var skipBtn = document.getElementById('skipOperatorBtn');
      var showOpBtn = document.getElementById('showOperatorFormBtn');
      var createBtn = document.getElementById('createOperatorBtn');
      var backBtn = document.getElementById('provisionedBackBtn');
      var formBackBtn = document.getElementById('operatorFormBackBtn');
      if (skipBtn) skipBtn.disabled = !!disabled;
      if (showOpBtn) showOpBtn.disabled = !!disabled;
      if (createBtn) createBtn.disabled = !!disabled;
      if (backBtn) backBtn.disabled = !!disabled;
      if (formBackBtn) formBackBtn.disabled = !!disabled;
    }

    function finishSetup(onDone, options) {
      if (finishSetupSubmitting) return;
      options = options || {};
      finishSetupSubmitting = true;
      setFinishButtonsDisabled(true);
      showFinishingUi();
      setFinishProgressLabel(FINISH_PROGRESS_LABELS.queued);
      var finishPayload = {
        portalDeploymentMode: options.portalDeploymentMode || 'manual_external'
      };
      var finishPortalStatus = null;
      var finishSucceeded = false;
      function finalizeFromStatus(json) {
        var data = (json && json.data) || {};
        setupStatus = data;
        if (finishPortalStatus) data.portalStatus = finishPortalStatus;
        if (typeof onDone === 'function') onDone();
        var installState = String(data.installationState || '').toLowerCase();
        var ready =
          !!data.productionMode ||
          installState === 'ready' ||
          installState === 'provisioned' ||
          finishSucceeded;
        if (ready) {
          finishSetupSubmitting = false;
          hideBusyOverlay();
          showSetupCompleteUi(data);
          return;
        }
        // Finish reported success but lifecycle is not provisioned yet — allow retry.
        finishSetupSubmitting = false;
        hideBusyOverlay();
        setFinishButtonsDisabled(false);
        resumeWizardFromStatus(json);
      }
      function failFinalize(res) {
        finishSetupSubmitting = false;
        hideBusyOverlay();
        setFinishButtonsDisabled(false);
        document.getElementById('productionReadyNotice').classList.add('hidden-block');
        document.getElementById('operatorOptionalSection').classList.remove('hidden-block');
        updateCompletePanelUi();
        if (handleSetupLifecycleError(res, 'operatorFormError',
            'Unable to finish setup. Please try again.')) {
          return;
        }
        showFormError('operatorFormError',
          (res && res.json && res.json.error) || 'Unable to finish setup. Please try again.');
      }
      function handleFinishResult(res) {
        if (!res.json || !res.json.success) {
          if (res.json && res.json.code === 'DEVICE_RESTARTED') {
            loadSetupStatus(finalizeFromStatus, failFinalize);
            return;
          }
          failFinalize(res);
          return;
        }
        finishSucceeded = true;
        if (res.json.data && res.json.data.portalStatus) {
          finishPortalStatus = res.json.data.portalStatus;
        }
        // Authoritative Ready/provisioned confirmation after Finish commits.
        loadSetupStatus(finalizeFromStatus, function () {
          // Status unreachable from Setup AP after production handoff is not
          // an installation failure — show completion with last known data.
          finalizeFromStatus({
            success: true,
            data: Object.assign({}, setupStatus || {}, {
              productionMode: true,
              installationState: 'ready',
              portalStatus: finishPortalStatus || (setupStatus && setupStatus.portalStatus)
            })
          });
        });
      }
      fetch('/api/setup/finish', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(finishPayload)
      })
        .then(function (r) { return r.json().then(function (j) { return { status: r.status, json: j }; }); })
        .then(function (res) {
          if (res.json && res.json.success && res.json.data && res.json.data.jobId) {
            setFinishProgressLabel(finishProgressLabel('queued'));
            pollRouterJob(res.json.data.jobId, handleFinishResult, function (jobOrState) {
              setFinishProgressLabel(finishProgressLabel(jobOrState));
            });
            return;
          }
          handleFinishResult(res);
        })
        .catch(failFinalize);
    }

    function loadSetupStatus(onReady, onError) {
      setLoading(true);
      fetch('/api/setup/status')
        .then(function (r) { return r.json(); })
        .then(function (json) {
          var data = (json && json.data) || {};
          setupStatus = data;
          syncSetupUnlockSession(data);
          if (data.productionMode) {
            setLoading(false);
            if (data.setupUnlockRequired && !data.setupUnlockSessionRemainingMs) {
              redirectToLockedSetup();
              return;
            }
            if (typeof onReady === 'function') {
              onReady(json);
              return;
            }
            enterProductionMode(data);
            return;
          }
          if (typeof onReady === 'function') {
            onReady(json);
            setLoading(false);
            return;
          }
          resumeWizardFromStatus(json);
          setLoading(false);
        })
        .catch(function () {
          setLoading(false);
          if (typeof onError === 'function') {
            onError();
            return;
          }
          showPanel('panelOwner');
        });
    }

    function refreshStepBars() {
      var current = panelStepIndex(activePanel);
      for (var b = 1; b <= 4; b++) {
        var bar = document.getElementById('stepBar' + b);
        if (activePanel === 'panelProvisioned') {
          bar.className = 'step done';
        } else if (b < current) {
          bar.className = 'step done';
        } else if (b === current) {
          bar.className = 'step active';
        } else {
          bar.className = 'step';
        }
      }
    }

    function panelStepIndex(name) {
      // PANEL_ORDER already reflects the visual step order (Owner, Router
      // Connection, Router Scan, Wi-Fi Configuration, Complete), so the
      // step-bar index is simply the panel's position.
      var idx = PANEL_ORDER.indexOf(name);
      if (idx < 0) return 1;
      return Math.min(idx + 1, 4);
    }

    function showPanel(name, options) {
      options = options || {};
      clearAllWizardValidation();
      activePanel = name;
      for (var i = 0; i < PANEL_ORDER.length; i++) {
        document.getElementById(PANEL_ORDER[i]).classList.remove('active');
      }
      document.getElementById(name).classList.add('active');

      var current = panelStepIndex(name);
      for (var b = 1; b <= 4; b++) {
        var bar = document.getElementById('stepBar' + b);
        if (name === 'panelProvisioned') {
          bar.className = 'step done';
        } else if (b < current) {
          bar.className = 'step done';
        } else if (b === current) {
          bar.className = 'step active';
        } else {
          bar.className = 'step';
        }
      }

      if (name === 'panelMikrotik') {
        applyRouterFormDefaultsFromStatus(setupStatus);
        updateMikrotikEthernetStatus(setupStatus);
      }
      if (name === 'panelWifi') {
        if (!setupWizardEnabled()) return;
        var np = (setupStatus && setupStatus.networkProvisioning) || {};
        if (np.externalApOnly) {
          setExternalApWizardMode(true);
        }
        loadWifiNetworks();
      }
      if (name === 'panelReview') {
        if (!setupWizardEnabled()) return;
        restoreExistingScanUi();
        if (options.beginExistingNetworkScan &&
            setupStatus && setupStatus.wizardStep === 'wifi' &&
            !lastExistingScanData && !activeExistingScanJobId && !scanSubmitting) {
          startExistingNetworkScan({ auto: true });
        }
      }
      if (name === 'panelProvisioned') {
        updateCompletePanelUi();
      }
      refreshStepBars();
    }

    function showFormError(elId, msg) {
      var el = document.getElementById(elId);
      if (!msg) {
        el.style.display = 'none';
        el.textContent = '';
        return;
      }
      el.style.display = 'block';
      el.textContent = msg;
    }

    function setLoading(on) {
      document.getElementById('globalLoading').style.display = on ? 'block' : 'none';
      document.getElementById('createBtn').disabled = on || submitting;
      if (on) {
        showBusyOverlay('Loading Setup', 'Loading your setup progress\u2026');
      } else if (!submitting && !routerSubmitting && !isApplyInFlight() &&
                 !wifiSaveInFlight && !scanSubmitting && !finishSetupSubmitting) {
        hideBusyOverlay();
      }
    }

    function updateMikrotikEthernetStatus(data) {
      data = data || setupStatus || {};
      var eth = data.ethernet || {};
      var net = data.network || {};

      var linkEl = document.getElementById('mikrotikLink');
      linkEl.textContent = eth.link ? 'Up' : 'Down';
      linkEl.className = eth.link ? 'ok' : 'warn';

      var ipEl = document.getElementById('mikrotikEspIp');
      if (eth.hasIp && eth.ip) {
        ipEl.textContent = eth.ip;
        ipEl.className = 'ok';
      } else if (net.ip) {
        ipEl.textContent = net.ip;
        ipEl.className = 'ok';
      } else {
        ipEl.textContent = 'Waiting for DHCP';
        ipEl.className = 'warn';
      }

      var gw = net.gateway || eth.gateway || '\u2014';
      document.getElementById('mikrotikGateway').textContent = gw;
    }

    function applyWizardPrefill(data) {
      data = data || {};
      if (data.ownerDisplayName) {
        document.getElementById('ownerDisplayName').value = data.ownerDisplayName;
      }
      if (data.ownerUsername) {
        document.getElementById('username').value = data.ownerUsername;
      }
    }

    function clearAllWizardValidation() {
      showFormError('ownerFormError', '');
      showFormError('existingScanError', '');
      showFormError('wifiFormError', '');
      showFormError('operatorFormError', '');
      showRouterFormError('');
      document.getElementById('routerFormSuccess').style.display = 'none';
      document.getElementById('routerFormSuccess').textContent = '';
    }

    function isSetupLifecycleErrorCode(code) {
      if (!code) return false;
      return code === 'SETUP_OWNER_REQUIRED' ||
        code === 'ROUTER_CONFIGURE_REQUIRED' ||
        code.indexOf('SETUP_') === 0;
    }

    function handleSetupLifecycleError(res, fallbackElId, fallbackMsg) {
      var json = res && res.json;
      if (json && json.code === 'SETUP_UNLOCK_REQUIRED') {
        redirectToLockedSetup();
        return true;
      }
      if (!isSetupLifecycleErrorCode(json && json.code)) return false;
      loadSetupStatus(function (statusJson) {
        resumeWizardFromStatus(statusJson);
      }, function () {
        if (fallbackElId) {
          showFormError(fallbackElId, fallbackMsg || 'Unable to refresh setup status.');
        }
      });
      return true;
    }

    function isWifiConfiguredFromStatus(data) {
      return isWifiSetupComplete(data);
    }

    function panelForWizardStep(step, data) {
      // Backend wizardStepForPhase: wifi -> Router Scan; review -> Wi-Fi Configuration.
      // Enforce sequential order: do not open Step 5 unless Wi-Fi is complete
      // (or productionMode — handled by the caller before this mapping).
      switch (step) {
        case 'owner': return 'panelOwner';
        case 'router': return 'panelMikrotik';
        case 'wifi':
        case 'applying':
          return 'panelReview';
        case 'review': return 'panelWifi';
        case 'complete':
          if (!isWifiConfiguredFromStatus(data)) {
            return 'panelWifi';
          }
          return 'panelProvisioned';
        default: return 'panelOwner';
      }
    }

    function syncWizardFormFromStatus(data) {
      data = data || {};
      var np = data.networkProvisioning || {};
      if (np.wifiSelectionConfigured) {
        wifiSelection.mode = np.wifiMode || 'existing';
        wifiSelection.interfaceId = np.interfaceId || '';
        wifiSelection.selectedSsid = np.selectedSsidHint || '';
        wifiSelection.ssid = np.selectedSsidHint || '';
        wifiNetworksLoaded = false;
        wifiNetworksRetryCount = 0;
      }
      if (np.externalApOnly) {
        setExternalApWizardMode(true);
      } else {
        setExternalApWizardMode(false);
      }
      if (data.networkProvisioning) {
        networkModeState = data.networkProvisioning;
      }
    }

    function applyWizardStepFromStatus(data, options) {
      options = options || {};
      data = data || {};
      setupStatus = data;
      syncSetupUnlockSession(data);
      applyWizardPrefill(data);
      syncWizardFormFromStatus(data);

      if (!setupWizardEnabled()) {
        enterProductionMode(data);
        return;
      }
      if (data.productionMode) {
        if (data.setupUnlockRequired && !data.setupUnlockSessionRemainingMs) {
          redirectToLockedSetup();
          return;
        }
        if (options.showInstallationSummary) {
          showSetupCompleteUi(data);
        } else {
          enterProductionMode(data);
        }
        return;
      }

      var step = data.wizardStep || 'owner';
      if (step === 'complete') {
        setupCompleteShown = true;
      }
      var panelOptions = {};
      if (step === 'wifi' && options.beginExistingNetworkScan) {
        panelOptions.beginExistingNetworkScan = true;
      }
      showPanel(panelForWizardStep(step, data), panelOptions);
    }

    function resumeWizardFromStatus(json, options) {
      var data = (json && json.data) || json || {};
      applyWizardStepFromStatus(data, options || {});
    }

    function formatRouterError(json) {
      if (!json) return 'Request failed';
      var code = json.code || (json.data && json.data.code) || '';
      var stage = json.stage || (json.data && json.data.stage) || '';
      var msg = json.error || json.errorMessage || json.message || 'Request failed';
      if (code && stage) return code + ': ' + stage + ': ' + msg;
      if (code) return code + ': ' + msg;
      return msg;
    }

    function updateWifiNextButtonState() {
      document.getElementById('wifiNextBtn').disabled =
        wifiNetworksLoading || applyExistingNetworkInFlight || wifiSaveInFlight;
    }

    function validateWifiStep() {
      if (externalApWizardMode) {
        wifiSelection.mode = 'external_ap';
        wifiSelection.interfaceId = '';
        wifiSelection.ssid = '';
        wifiSelection.selectedSsid = '';
        wifiSelection.password = '';
        return '';
      }
      var existingChecked = document.getElementById('wifiModeExisting').checked;
      var newChecked = document.getElementById('wifiModeNew').checked;
      if (!existingChecked && !newChecked) {
        return 'Please choose Existing SSID or Create New SSID.';
      }
      if (existingChecked) {
        wifiSelection.mode = 'existing';
        syncWifiExistingSelection();
        if (!wifiSelection.interfaceId) {
          return 'Please choose an existing SSID.';
        }
        return '';
      }
      wifiSelection.mode = 'new';
      wifiSelection.ssid = document.getElementById('wifiNewSsid').value.trim();
      wifiSelection.password = '';
      if (!wifiSelection.ssid) {
        return 'Please enter an SSID.';
      }
      return '';
    }

    function cancelWifiDiscoveryRetry() {
      if (wifiDiscoveryRetryTimer) {
        clearTimeout(wifiDiscoveryRetryTimer);
        wifiDiscoveryRetryTimer = null;
      }
    }

    function scheduleWifiDiscoveryRetry() {
      if (wifiDiscoveryRetryTimer) return;
      wifiDiscoveryRetryTimer = setTimeout(function () {
        wifiDiscoveryRetryTimer = null;
        loadWifiNetworks();
      }, WIFI_NETWORKS_RETRY_MS);
    }

    function setWifiMode(mode) {
      wifiSelection.mode = mode === 'new' ? 'new' : 'existing';
      var useExisting = wifiSelection.mode === 'existing';
      document.getElementById('wifiModeExisting').checked = useExisting;
      document.getElementById('wifiModeNew').checked = !useExisting;
      document.getElementById('wifiExistingSelect').disabled = !useExisting || wifiNetworksLoading;
      document.getElementById('wifiNewSsid').disabled = useExisting;
      if (useExisting) {
        syncWifiExistingSelection();
      } else {
        wifiSelection.interfaceId = '';
        wifiSelection.selectedSsid = '';
        wifiSelection.ssid = document.getElementById('wifiNewSsid').value.trim();
        wifiSelection.password = '';
      }
      updateWifiNextButtonState();
    }

    function syncWifiExistingSelection() {
      var select = document.getElementById('wifiExistingSelect');
      var option = select.options[select.selectedIndex];
      wifiSelection.interfaceId = select.value || '';
      wifiSelection.selectedSsid = '';
      wifiSelection.ssid = '';
      wifiSelection.password = '';
      if (wifiSelection.interfaceId && availableWifiNetworks.length) {
        for (var i = 0; i < availableWifiNetworks.length; i++) {
          if (availableWifiNetworks[i].id === wifiSelection.interfaceId) {
            wifiSelection.selectedSsid =
              availableWifiNetworks[i].ssid || availableWifiNetworks[i].id;
            break;
          }
        }
      }
      if (!wifiSelection.selectedSsid && option) {
        wifiSelection.selectedSsid = option.textContent || '';
      }
      updateWifiNextButtonState();
    }

    function formatWifiNetworkLabel(n) {
      var label = n.ssid || n.id || 'Unnamed interface';
      if (n.status === 'disabled') label += ' (Disabled)';
      else if (n.status === 'no_ssid') label += ' (no SSID)';
      else if (n.status === 'hidden') label += ' (hidden)';
      if (n.securityOpen === false) label += ' [secured]';
      return label;
    }

    function renderWifiNetworks(networks) {
      var select = document.getElementById('wifiExistingSelect');
      select.innerHTML = '';
      if (!networks || !networks.length) {
        select.innerHTML = '<option value="">No SSIDs found</option>';
        select.disabled = true;
        return;
      }
      for (var i = 0; i < networks.length; i++) {
        var n = networks[i];
        var opt = document.createElement('option');
        opt.value = n.id;
        opt.textContent = formatWifiNetworkLabel(n);
        if (n.status === 'disabled') opt.dataset.disabledSsid = '1';
        select.appendChild(opt);
      }
      select.disabled = wifiSelection.mode !== 'existing';
      syncWifiExistingSelection();
    }

    var wifiNetworksRetryCount = 0;
    var WIFI_NETWORKS_MAX_RETRIES = 12;
    var WIFI_NETWORKS_RETRY_MS = 2500;

    function loadWifiNetworks() {
      if (wifiNetworksLoading || wifiNetworksLoaded) return;
      cancelWifiDiscoveryRetry();
      wifiNetworksLoading = true;
      updateWifiNextButtonState();
      var notice = document.getElementById('wifiNetworksNotice');
      notice.textContent = 'Loading available SSIDs\u2026';
      showFormError('wifiFormError', '');
      showBusyOverlay('Loading Wi-Fi Networks', 'Reading SSIDs from your MikroTik\u2026');
      fetch('/api/setup/router/wifi/networks', { cache: 'no-store' })
        .then(function (r) { return r.json().then(function (j) { return { status: r.status, json: j }; }); })
        .then(function (res) {
          var json = res.json || {};

          if (res.status === 202 || res.status === 503 ||
              json.status === 'busy' ||
              json.code === 'ROUTER_WORKER_BUSY' ||
              json.code === 'ETH_DMA_LOW') {
            wifiNetworksLoading = false;
            updateWifiNextButtonState();
            if (wifiNetworksRetryCount < WIFI_NETWORKS_MAX_RETRIES) {
              wifiNetworksRetryCount++;
              notice.textContent =
                json.code === 'ETH_DMA_LOW'
                  ? 'Waiting for free memory, then loading SSIDs\u2026'
                  : 'Checking MikroTik Wi-Fi networks\u2026';
              updateBusyOverlay(notice.textContent);
              scheduleWifiDiscoveryRetry();
            } else {
              cancelWifiDiscoveryRetry();
              hideBusyOverlay();
              wifiNetworksLoaded = true;
              notice.textContent = 'Still checking MikroTik. You can create a new SSID instead, or wait and reopen this step.';
              setWifiMode('new');
              updateWifiNextButtonState();
            }
            return;
          }

          cancelWifiDiscoveryRetry();
          wifiNetworksRetryCount = 0;
          wifiNetworksLoading = false;
          wifiNetworksLoaded = true;
          updateWifiNextButtonState();
          hideBusyOverlay();
          var networks = json.data || [];
          var code = json.code || '';
          availableWifiNetworks = networks;
          document.getElementById('wifiModeExisting').disabled = false;

          if (!json.success) {
            notice.textContent =
              (json.error || 'Unable to load SSIDs.') + ' You can create a new SSID instead.';
            availableWifiNetworks = [];
            setWifiMode('new');
            return;
          }

          if (code === 'WIFI_NO_WIRELESS_PACKAGE' || code === 'WIFI_NO_INTERFACES') {
            setExternalApWizardMode(true);
            return;
          }

          if (code === 'WIFI_NO_SSIDS') {
            notice.textContent =
              json.message || 'No SSIDs are configured yet. Create a dedicated open Piso Wi-Fi network.';
            setWifiMode('new');
            return;
          }

          if (code === 'WIFI_ALL_DISABLED') {
            notice.textContent =
              'Existing SSIDs were found but all wireless interfaces are disabled. Select one to enable during setup, or create a new SSID.';
            renderWifiNetworks(networks);
            setWifiMode('existing');
            return;
          }

          if (!networks.length) {
            notice.textContent =
              json.message || 'No SSIDs are configured yet. Create a dedicated open Piso Wi-Fi network.';
            setWifiMode('new');
            return;
          }

          notice.textContent = json.message || 'Select the SSID customers will use for Piso Wi-Fi.';
          renderWifiNetworks(networks);
          setWifiMode('existing');
        })
        .catch(function () {
          cancelWifiDiscoveryRetry();
          wifiNetworksLoading = false;
          wifiNetworksLoaded = true;
          updateWifiNextButtonState();
          hideBusyOverlay();
          notice.textContent = 'Unable to load SSIDs. You can create a new SSID instead.';
          setWifiMode('new');
        });
    }

    function saveWifiSelectionAndContinue() {
      showFormError('wifiFormError', '');
      if (wifiNetworksLoading) {
        showFormError('wifiFormError', 'Still loading SSIDs. Please wait a moment.');
        return;
      }
      var validationError = validateWifiStep();
      if (validationError) {
        showFormError('wifiFormError', validationError);
        return;
      }
      var payload = { wifiMode: wifiSelection.mode };
      if (wifiSelection.mode === 'external_ap') {
        payload.externalApOnly = true;
      } else if (wifiSelection.mode === 'existing') {
        payload.interfaceId = wifiSelection.interfaceId;
        payload.selectedSsid = wifiSelection.selectedSsid;
      } else {
        payload.ssid = wifiSelection.ssid;
      }
      wifiSaveInFlight = true;
      updateWifiNextButtonState();
      showBusyOverlay('Saving Wi-Fi Selection', 'Saving your Wi-Fi choice\u2026');
      fetch('/api/setup/router/wifi/selection', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        })
          .then(function (r) { return r.json().then(function (j) { return { status: r.status, json: j }; }); })
          .then(function (res) {
            if (!res.json || !res.json.success) {
              wifiSaveInFlight = false;
              updateWifiNextButtonState();
              hideBusyOverlay();
              if (handleSetupLifecycleError(res, 'wifiFormError',
                  (res.json && res.json.error) || 'Unable to save Wi-Fi selection')) {
                return;
              }
              showFormError('wifiFormError', (res.json && res.json.error) || 'Unable to save Wi-Fi selection');
              return;
            }
            if (res.json.data) setupStatus = res.json.data;
            updateBusyOverlay('Persisting Wi-Fi selection\u2026', 35);
            return waitForWifiSelectionDurable(res).then(function () {
              wifiSaveInFlight = false;
              updateWifiNextButtonState();
              updateBusyOverlay('Applying configuration on your router\u2026', 55);
              finishWifiStepAndApply();
            });
          })
          .catch(function (err) {
            wifiSaveInFlight = false;
            updateWifiNextButtonState();
            hideBusyOverlay();
            showFormError('wifiFormError',
              (err && err.message) || 'Unable to save Wi-Fi selection');
          });
    }

    function waitForWifiSelectionDurable(initialRes) {
      var data = (initialRes && initialRes.json && initialRes.json.data) || {};
      var np = data.networkProvisioning || data;
      var status = String(data.durableCommitStatus || np.durableCommitStatus || '')
        .toUpperCase();
      function isPending(st) {
        return st === 'QUEUED' || st === 'PERSISTING' || st === 'PENDING';
      }
      function isFailed(st, net) {
        return st === 'FAILED' || !!(net && net.durableCommitError);
      }
      function isPersisted(st) {
        return st === 'PERSISTED' || st === '';
      }
      if (isFailed(status, np)) {
        return Promise.reject(new Error(
          data.durableCommitError || np.durableCommitError ||
          'Unable to persist Wi-Fi selection'));
      }
      if (!isPending(status) && initialRes.status !== 202) {
        return Promise.resolve();
      }
      var deadline = Date.now() + 15000;
      function poll() {
        return fetch('/api/setup/status', { cache: 'no-store' })
          .then(function (r) { return r.json(); })
          .then(function (json) {
            if (json && json.data) setupStatus = json.data;
            var net = (json && json.data && json.data.networkProvisioning) || {};
            var st = String(net.durableCommitStatus || '').toUpperCase();
            if (isFailed(st, net)) {
              throw new Error(net.durableCommitError ||
                'Unable to persist Wi-Fi selection');
            }
            if (st === 'PERSISTED' ||
                (net.wifiSelectionConfigured &&
                 net.wifiSelectionDurablePending !== true &&
                 !isPending(st))) {
              return;
            }
            if (Date.now() >= deadline) {
              throw new Error('Timed out waiting for Wi-Fi selection to persist');
            }
            return new Promise(function (resolve) {
              setTimeout(resolve, 250);
            }).then(poll);
          });
      }
      return poll();
    }

    // Router Scan (Step 3) already produced and cached selectedExistingCandidate
    // before the user reached Wi-Fi Configuration (Step 4). Finishing this step
    // applies that same cached decision — it does not scan or reconnect to
    // RouterOS again. If the in-memory scan state was lost (e.g. the browser
    // was reloaded mid-wizard), send the user back to Step 3 rather than
    // silently starting a new scan.
    function finishWifiStepAndApply() {
      if (selectedExistingCandidate && scanConfirmEnabled(lastExistingScanData)) {
        wifiAdoptionDmaRetries = 0;
        executeAdoption();
        return;
      }
      wifiSaveInFlight = false;
      updateWifiNextButtonState();
      hideBusyOverlay();
      showFormError(
        'wifiFormError',
        'Router scan result is no longer available. Please go back and confirm the Router Scan step again.');
    }

    function executeAdoption() {
      var c = selectedExistingCandidate;
      if (!c || isApplyInFlight()) return;
      applyExistingNetworkInFlight = true;
      document.getElementById('adoptExistingBtn').disabled = true;
      showFormError('existingScanError', '');
      showAdoptProgressModal('Applying Configuration...');
      updateExistingScanButtonState();

      function handleAdoptionResult(res) {
        applyExistingNetworkInFlight = false;
        if (!res.json || !res.json.success) {
          var dmaBusy = (res.status === 503) ||
              (res.json && (res.json.code === 'ETH_DMA_LOW' ||
                            res.json.code === 'ROUTER_WORKER_BUSY'));
          if (dmaBusy && wifiAdoptionDmaRetries < 8) {
            wifiAdoptionDmaRetries += 1;
            showBusyOverlay('Applying Configuration',
              res.json && res.json.code === 'ROUTER_WORKER_BUSY'
                ? 'Router is still applying your configuration\u2026'
                : 'Waiting for free memory, then applying\u2026');
            // Re-POST is safe: worker joins the in-flight configure jobId.
            setTimeout(function () { executeAdoption(); }, 2000);
            return;
          }
          var errMsg = formatRouterError(res.json);
          if (handleSetupLifecycleError(res, 'existingScanError', errMsg)) {
            hideBusyOverlay();
            updateAdoptButtonState();
            updateExistingScanButtonState();
            updateWifiNextButtonState();
            return;
          }
          showAdoptFailureModal(errMsg);
          showFormError('existingScanError', errMsg);
          updateAdoptButtonState();
          updateExistingScanButtonState();
          updateWifiNextButtonState();
          return;
        }
        if (res.json.data) {
          setupStatus = res.json.data;
          if (res.json.data.networkProvisioning) {
            networkModeState = res.json.data.networkProvisioning;
          }
        }
        wifiAdoptionDmaRetries = 0;
        showSetupCompletePanel(res.json);
      }

      fetch('/api/setup/router/existing-network/configure', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          confirmation: 'ADOPT EXISTING RENZ-FI NETWORK',
          wifiMode: externalApWizardMode ? 'external_ap' : wifiSelection.mode,
          externalApOnly: externalApWizardMode || wifiSelection.mode === 'external_ap',
          interfaceId: wifiSelection.mode === 'existing' ? wifiSelection.interfaceId : undefined,
          ssid: wifiSelection.mode === 'new' ? wifiSelection.ssid : undefined,
          selectedSsid: wifiSelection.mode === 'existing' ? wifiSelection.selectedSsid : undefined,
          candidateId: c.id,
          bridgeName: c.bridgeName,
          gatewayCidr: c.gatewayCidr,
          guestNetwork: c.guestNetwork,
          gatewayIp: c.gatewayIp,
          poolName: c.poolName,
          poolRange: c.poolRange,
          dhcpServerName: c.dhcpServerName,
          dhcpNetwork: c.dhcpNetwork,
          status: c.status,
          origin: c.origin,
          confidence: c.confidence,
          apiAccessOk: c.apiAccessOk,
          scanId: lastExistingScanData && lastExistingScanData.scanId,
          confirmCandidateId: c.id,
          confirmAllowed: !!(lastExistingScanData && lastExistingScanData.confirmAllowed),
          hotspotDetected: !!(lastExistingScanData && lastExistingScanData.hotspotDetected)
        })
      })
        .then(function (r) { return r.json().then(function (j) { return { status: r.status, json: j }; }); })
        .then(function (res) {
          if (res.json && res.json.success && res.json.data && res.json.data.jobId) {
            updateBusyOverlay('Applying configuration on your router\u2026', 70);
            pollRouterJob(res.json.data.jobId, handleAdoptionResult, function (job) {
              var label = (job && job.stageLabel) ||
                (job && job.state === 'queued'
                  ? 'Queued on device\u2026'
                  : 'Applying configuration on your router\u2026');
              updateBusyOverlay(label, job && job.state === 'running' ? 82 : 70);
            });
            return;
          }
          handleAdoptionResult(res);
        })
        .catch(function () {
          applyExistingNetworkInFlight = false;
          showAdoptFailureModal('Network error while applying configuration');
          showFormError('existingScanError', 'Configuration failed');
          updateAdoptButtonState();
          updateExistingScanButtonState();
          updateWifiNextButtonState();
        });
    }

    function showRouterFormError(msg) {
      var el = document.getElementById('routerFormError');
      if (!msg) {
        el.style.display = 'none';
        el.textContent = '';
        return;
      }
      document.getElementById('routerFormSuccess').style.display = 'none';
      el.style.display = 'block';
      el.textContent = msg;
    }

    function showRouterFormSuccess(msg) {
      var el = document.getElementById('routerFormSuccess');
      el.style.display = 'block';
      el.textContent = msg;
      showRouterFormError('');
    }

    function routerPayload() {
      return {
        host: document.getElementById('routerHost').value.trim(),
        username: document.getElementById('routerUsername').value.trim(),
        password: document.getElementById('routerPassword').value,
        apiPort: parseInt(document.getElementById('routerPort').value, 10) || 8728
      };
    }

    function routerSavePayload() {
      return routerPayload();
    }

    function invalidateRouterConnectionTest() {
      routerTestOk = false;
      routerConnectionId = null;
      routerTestSnapshot = null;
      updateRouterSaveButtonState();
    }

    function updateRouterSaveButtonState() {
      var saveEnabled = routerTestOk && !routerSubmitting;
      document.getElementById('saveRouterBtn').disabled = !saveEnabled;
    }

    function routerFormChangedSinceTest() {
      if (!routerTestSnapshot) return true;
      var payload = routerPayload();
      return payload.host !== routerTestSnapshot.host ||
        payload.username !== routerTestSnapshot.username ||
        payload.password !== routerTestSnapshot.password ||
        payload.apiPort !== routerTestSnapshot.apiPort;
    }

    function onRouterFormEdited() {
      hideRouterSaveSuccessModal();
      if (routerTestOk && routerFormChangedSinceTest()) {
        invalidateRouterConnectionTest();
        showRouterFormSuccess('');
      }
    }

    function setRouterBusy(on) {
      document.getElementById('testRouterBtn').disabled = on || routerSubmitting;
      document.getElementById('mikrotikBackBtn').disabled = on || routerSubmitting;
      updateRouterSaveButtonState();
    }

    function applyRouterFormDefaultsFromStatus(data) {
      data = data || setupStatus || {};
      var eth = data.ethernet || {};
      var net = data.network || {};
      var gateway = eth.gateway || net.gateway || '';
      if (gateway && !document.getElementById('routerHost').value.trim()) {
        document.getElementById('routerHost').value = gateway;
      }
      if (!document.getElementById('routerUsername').value.trim()) {
        document.getElementById('routerUsername').value = 'admin';
      }
      if (!document.getElementById('routerPort').value) {
        document.getElementById('routerPort').value = '8728';
      }
      document.getElementById('routerPassword').value = '';
      routerHasSavedPassword = false;
      invalidateRouterConnectionTest();
      if (gateway) savedRouterHost = gateway;
      updateRouterSaveButtonState();
    }

    function loadRouterConfig(onReady) {
      // Step 2 uses form-only credentials; do not read persisted router-connection cache.
      applyRouterFormDefaultsFromStatus(setupStatus);
      if (typeof onReady === 'function') onReady();
    }

    function validateRouterPayload() {
      var payload = routerPayload();
      if (!payload.host || !payload.username) {
        showRouterFormError('Router IP and username are required.');
        return null;
      }
      if (!payload.password) {
        showRouterFormError('Router password is required.');
        return null;
      }
      return payload;
    }

    function pollRouterJob(jobId, onDone, onProgress) {
      var attempts = 0;
      function tick() {
        attempts++;
        fetch('/api/setup/router/jobs/' + jobId, { cache: 'no-store' })
          .then(function (r) {
            return r.json().then(function (j) { return { status: r.status, json: j }; });
          })
          .then(function (resp) {
            if (resp.status === 404 &&
                resp.json && resp.json.code === 'JOB_NOT_FOUND') {
              onDone({
                status: 404,
                json: {
                  success: false,
                  error: 'Device restarted while processing the request. No router changes were applied. Reconnect and try again.',
                  code: 'DEVICE_RESTARTED'
                }
              });
              return;
            }
            var json = resp.json || {};
            // Transient SoftAP/DMA pressure must not abort an in-flight job.
            // Proven false failure: poll 503 ETH_DMA_LOW → UI re-POST configure
            // → ROUTER_WORKER_BUSY shown as "Configuration failed" while the
            // original job later logged ADOPTION COMPLETE.
            if (resp.status === 503 ||
                json.code === 'ETH_DMA_LOW' ||
                json.code === 'ROUTER_WORKER_BUSY') {
              if (onProgress) {
                onProgress({
                  state: 'running',
                  stageLabel: json.code === 'ETH_DMA_LOW'
                    ? 'Waiting for free memory\u2026'
                    : 'Waiting for router worker\u2026'
                });
              }
              if (attempts < 120) {
                setTimeout(tick, 800);
              } else {
                onDone({
                  status: 504,
                  json: {
                    success: false,
                    error: 'Router job timed out while waiting for free memory',
                    code: 'ROUTER_JOB_TIMEOUT'
                  }
                });
              }
              return;
            }
            var d = json.data || {};
            if (d.state === 'queued' || d.state === 'running') {
              if (onProgress) onProgress(d);
              if (attempts < 120) {
                setTimeout(tick, 400);
              } else {
                onDone({ status: 504, json: { success: false, error: 'Router job timed out', code: 'ROUTER_JOB_TIMEOUT' } });
              }
              return;
            }
            if (d.state === 'failed') {
              onDone({
                status: d.httpStatus || 500,
                json: d.result || {
                  success: false,
                  error: json.error || 'Router job failed',
                  code: d.code || 'ROUTER_JOB_FAILED'
                }
              });
              return;
            }
            if (d.result) {
              onDone({ status: d.httpStatus || 200, json: d.result });
              return;
            }
            if (json.success && !d.state && !d.result && json.data) {
              onDone({ status: resp.status || 200, json: json });
              return;
            }
            onDone({ status: 500, json: json.success === false ? json : { success: false, error: 'Invalid job response' } });
          })
          .catch(function () {
            // SoftAP blips are common; keep polling the same jobId.
            if (attempts < 120) {
              if (onProgress) {
                onProgress({ state: 'running', stageLabel: 'Reconnecting to device\u2026' });
              }
              setTimeout(tick, 800);
              return;
            }
            onDone({ status: 0, json: { success: false, error: 'Network error', code: 'NETWORK_ERROR' } });
          });
      }
      tick();
    }

    function postRouter(path, onDone, onProgress, requestBody) {
      setRouterBusy(true);
      routerSubmitting = true;
      var isTest = path.indexOf('/test') >= 0;
      showBusyOverlay(
        isTest ? 'Testing Router Connection' : 'Saving Router Connection',
        isTest ? 'Contacting MikroTik\u2026' : 'Saving router credentials\u2026');
      fetch(path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(requestBody || routerPayload())
      })
        .then(function (r) { return r.json().then(function (j) { return { status: r.status, json: j }; }); })
        .then(function (res) {
          if (res.json && res.json.success && res.json.data && res.json.data.jobId) {
            if (onProgress) onProgress('queued');
            updateBusyOverlay(
              isTest ? 'Waiting for router test result\u2026' : 'Waiting for save to complete\u2026',
              40);
            pollRouterJob(res.json.data.jobId, function (finalRes) {
              routerSubmitting = false;
              setRouterBusy(false);
              hideBusyOverlay();
              onDone(finalRes);
            }, function (job) {
              if (onProgress) onProgress(job);
              updateBusyOverlay(
                isTest ? 'Testing connection\u2026' : 'Saving connection\u2026',
                job && job.state === 'running' ? 75 : 45);
            });
            return;
          }
          routerSubmitting = false;
          setRouterBusy(false);
          hideBusyOverlay();
          onDone(res);
        })
        .catch(function () {
          routerSubmitting = false;
          setRouterBusy(false);
          hideBusyOverlay();
          showRouterFormError('Network error \u2014 please try again.');
        });
    }

    function postSetupStep(path, payload, errorElId, onSuccess, onFinally) {
      function done() {
        if (typeof onFinally === 'function') onFinally();
      }
      fetch(path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      })
        .then(function (r) { return r.json().then(function (j) { return { status: r.status, json: j }; }); })
        .then(function (res) {
          if (res.json && res.json.success) {
            resumeWizardFromStatus(res.json);
            if (typeof onSuccess === 'function') onSuccess(res);
            done();
            return;
          }
          if (handleSetupLifecycleError(res, errorElId,
              (res.json && res.json.error) || 'Request failed.')) {
            done();
            return;
          }
          showFormError(errorElId, (res.json && res.json.error) || 'Request failed.');
          done();
        })
        .catch(function () {
          showFormError(errorElId, 'Network error \u2014 please try again.');
          done();
        });
    }

    document.getElementById('createBtn').addEventListener('click', function () {
      if (submitting) return;
      // Unlock may happen on another client (phone vs laptop). Owner already
      // exists — Next must resume the shared wizard, not silently no-op.
      if (setupStatus && setupStatus.ownerCreated) {
        loadSetupStatus(function (json) {
          resumeWizardFromStatus(json);
        });
        return;
      }
      showFormError('ownerFormError', '');
      var payload = {
        displayName: document.getElementById('ownerDisplayName').value.trim(),
        username: document.getElementById('username').value.trim(),
        password: document.getElementById('password').value,
        confirmPassword: document.getElementById('confirmPassword').value,
        setupUnlockPassword: document.getElementById('setupUnlockPassword').value,
        confirmSetupUnlockPassword: document.getElementById('confirmSetupUnlockPassword').value
      };
      if (!payload.username || !payload.password) {
        showFormError('ownerFormError', 'Username and password are required.');
        return;
      }
      if (!payload.setupUnlockPassword || payload.setupUnlockPassword.length < 8) {
        showFormError('ownerFormError', 'Setup Unlock Password must be at least 8 characters.');
        return;
      }
      if (payload.setupUnlockPassword !== payload.confirmSetupUnlockPassword) {
        showFormError('ownerFormError', 'Setup Unlock Password confirmation does not match.');
        return;
      }
      submitting = true;
      document.getElementById('createBtn').disabled = true;
      showBusyOverlay('Creating Owner Account', 'Saving owner account\u2026');
      postSetupStep('/api/setup/owner', payload, 'ownerFormError', function () {
        document.getElementById('password').value = '';
        document.getElementById('confirmPassword').value = '';
        document.getElementById('setupUnlockPassword').value = '';
        document.getElementById('confirmSetupUnlockPassword').value = '';
        hideBusyOverlay();
      }, function () {
        submitting = false;
        document.getElementById('createBtn').disabled = false;
        hideBusyOverlay();
      });
    });

    document.getElementById('mikrotikBackBtn').addEventListener('click', function () {
      if (routerSubmitting) return;
      hideRouterSaveSuccessModal();
      showRouterFormError('');
      showPanel('panelOwner');
    });

    document.getElementById('testRouterBtn').addEventListener('click', function () {
      if (routerSubmitting) return;
      showRouterFormError('');
      hideRouterSaveSuccessModal();
      if (!validateRouterPayload()) return;
      var testedPayload = routerPayload();
      postRouter('/api/setup/router/test', function (res) {
        if (res.json && res.json.success) {
          var data = res.json.data || {};
          routerTestOk = true;
          routerConnectionId = null;
          routerTestSnapshot = {
            host: testedPayload.host,
            username: testedPayload.username,
            password: testedPayload.password,
            apiPort: testedPayload.apiPort
          };
          updateRouterSaveButtonState();
          var identity = data.routerIdentity || 'Router';
          var board = data.routerBoard ? (' · ' + data.routerBoard) : '';
          var version = data.routerOs ? (' · RouterOS ' + data.routerOs) : '';
          showRouterFormSuccess(
            '\u2713 Router reachable. Authentication successful.\n' +
            identity + board + version);
          return;
        }
        invalidateRouterConnectionTest();
        if (handleSetupLifecycleError(res, 'routerFormError',
            formatRouterError(res.json) || 'Router connection test failed.')) {
          return;
        }
        showRouterFormError(formatRouterError(res.json) || 'Router connection test failed.');
      }, function (state) {
        showRouterFormSuccess('Testing connection (' + state + ')...');
      });
    });

    document.getElementById('saveRouterBtn').addEventListener('click', function () {
      if (routerSubmitting || !routerTestOk) return;
      showRouterFormError('');
      hideRouterSaveSuccessModal();
      if (!validateRouterPayload()) return;
      if (routerFormChangedSinceTest()) {
        invalidateRouterConnectionTest();
        showRouterFormError('Router settings changed after the last test. Test the connection again before saving.');
        return;
      }
      postRouter('/api/setup/router/save', function (res) {
        if (res.json && res.json.success) {
          document.getElementById('routerPassword').value = '';
          routerHasSavedPassword = true;
          savedRouterHost = routerPayload().host;
          if (res.json.data) setupStatus = res.json.data;
          showRouterFormSuccess('\u2713 Router connection saved successfully.');
          showRouterSaveSuccessModal();
          return;
        }
        if (handleSetupLifecycleError(res, 'routerFormError',
            formatRouterError(res.json) || 'Unable to save router connection.')) {
          return;
        }
        showRouterFormError(formatRouterError(res.json) || 'Unable to save router connection.');
      }, function (state) {
        showRouterFormSuccess('Saving credentials (' + state + ')...');
      }, routerSavePayload());
    });

    document.getElementById('wifiBackBtn').addEventListener('click', function () {
      showPanel('panelReview');
    });

    document.getElementById('wifiNextBtn').addEventListener('click', function () {
      saveWifiSelectionAndContinue();
    });

    document.getElementById('wifiModeExisting').addEventListener('change', function () {
      if (this.checked) setWifiMode('existing');
    });
    document.getElementById('wifiModeNew').addEventListener('change', function () {
      if (this.checked) setWifiMode('new');
    });
    document.getElementById('wifiExistingSelect').addEventListener('change', syncWifiExistingSelection);
    document.getElementById('wifiNewSsid').addEventListener('input', function () {
      if (document.getElementById('wifiModeNew').checked) {
        wifiSelection.mode = 'new';
        wifiSelection.ssid = this.value.trim();
        wifiSelection.password = '';
      }
    });

    document.getElementById('reviewBackBtn').addEventListener('click', function () {
      if (isApplyInFlight()) return;
      showPanel('panelMikrotik');
    });

    document.getElementById('scanExistingBtn').addEventListener('click', function () {
      if (scanSubmitting || activeExistingScanJobId || isApplyInFlight()) return;
      startExistingNetworkScan({ auto: false });
    });

    document.getElementById('adoptExistingBtn').addEventListener('click', function () {
      // Step 3 Confirm no longer applies RouterOS changes directly — it
      // just accepts the (cached) scan result and moves on to Step 4
      // Wi-Fi Configuration. The actual configure/apply call
      // (executeAdoption, unchanged) fires once Wi-Fi is chosen there.
      if (isApplyInFlight() || !selectedExistingCandidate) return;
      showPanel('panelWifi');
    });

    function selectedPortalDeploymentMode() {
      var checked = document.querySelector('input[name="portalDeploy"]:checked');
      if (checked && checked.value) return checked.value;
      return 'skipped';
    }

    document.getElementById('skipOperatorBtn').addEventListener('click', function () {
      if (finishSetupSubmitting) return;
      // Skip Operator only. Portal mode comes from the separate radio choice.
      finishSetup(null, { portalDeploymentMode: selectedPortalDeploymentMode() });
    });

    document.getElementById('showOperatorFormBtn').addEventListener('click', function () {
      if (finishSetupSubmitting) return;
      document.getElementById('operatorIntroActions').classList.add('hidden-block');
      document.getElementById('operatorFormSection').classList.remove('hidden-block');
    });

    document.getElementById('provisionedBackBtn').addEventListener('click', function () {
      if (finishSetupSubmitting) return;
      // UI-only: return to Wi-Fi Configuration. No API calls / lifecycle changes.
      showPanel('panelWifi');
    });

    document.getElementById('operatorFormBackBtn').addEventListener('click', function () {
      if (finishSetupSubmitting) return;
      document.getElementById('operatorFormSection').classList.add('hidden-block');
      document.getElementById('operatorIntroActions').classList.remove('hidden-block');
    });

    document.getElementById('createOperatorBtn').addEventListener('click', function () {
      if (operatorSubmitting || finishSetupSubmitting) return;
      showFormError('operatorFormError', '');
      var payload = {
        displayName: document.getElementById('operatorDisplayName').value.trim(),
        username: document.getElementById('operatorUsername').value.trim(),
        password: document.getElementById('operatorPassword').value,
        confirmPassword: document.getElementById('operatorConfirmPassword').value
      };
      if (!payload.username || !payload.password) {
        showFormError('operatorFormError', 'Username and password are required.');
        return;
      }
      operatorSubmitting = true;
      setFinishButtonsDisabled(true);
      showBusyOverlay('Creating Operator', 'Saving operator account\u2026');
      fetch('/api/setup/operator', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      })
        .then(function (r) { return r.json().then(function (j) { return { status: r.status, json: j }; }); })
        .then(function (res) {
          operatorSubmitting = false;
          if (res.json && res.json.success) {
            // Finish with the independently selected portal deployment mode.
            finishSetup(null, { portalDeploymentMode: selectedPortalDeploymentMode() });
            return;
          }
          hideBusyOverlay();
          setFinishButtonsDisabled(false);
          if (handleSetupLifecycleError(res, 'operatorFormError',
              (res.json && res.json.error) || 'Unable to create operator account.')) {
            return;
          }
          showFormError('operatorFormError', (res.json && res.json.error) || 'Unable to create operator account.');
        })
        .catch(function () {
          operatorSubmitting = false;
          hideBusyOverlay();
          setFinishButtonsDisabled(false);
          showFormError('operatorFormError', 'Network error — please try again.');
        });
    });

    document.getElementById('cancelSetupBtn').addEventListener('click', cancelUnlockedSetup);

    ['routerHost', 'routerUsername', 'routerPassword', 'routerPort'].forEach(function (id) {
      var el = document.getElementById(id);
      if (el) {
        el.addEventListener('input', function (ev) {
          if (ev && ev.isTrusted === false) return;
          invalidateRouterConnectionTest();
        });
        el.addEventListener('change', function (ev) {
          if (ev && ev.isTrusted === false) return;
          invalidateRouterConnectionTest();
        });
      }
    });

    loadSetupStatus();
  </script>
</body>
</html>)rawliteral";
