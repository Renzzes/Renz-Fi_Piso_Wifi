#!/usr/bin/env python3
"""Regression guards for 4-phase linear wizard navigation."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"
SETUP = ROOT.parent / "src" / "web" / "SetupServer.cpp"
PROV = ROOT.parent / "src" / "SetupProvisioningManager.cpp"
API = ROOT.parent / "src" / "ApiServer.cpp"
WEB = ROOT.parent / "src" / "web" / "WebServerManager.cpp"
HANDOFF = ROOT.parent / "src" / "ProductionHandoff.cpp"
WORKER = ROOT.parent / "src" / "RouterProvisioningWorker.cpp"
STATUS_CTX = ROOT.parent / "src" / "SetupStatusContext.h"

WIZARD_STEPS = (
    "owner",
    "router",
    "wifi",
    "review",
    "applying",
    "complete",
)

PANELS = (
    "panelOwner",
    "panelMikrotik",
    "panelWifi",
    "panelReview",
    "panelProvisioned",
)


def main() -> int:
    errors: list[str] = []
    html = HTML.read_text(encoding="utf-8")
    setup = SETUP.read_text(encoding="utf-8")
    prov = PROV.read_text(encoding="utf-8")
    api = API.read_text(encoding="utf-8")
    web = WEB.read_text(encoding="utf-8")
    handoff = HANDOFF.read_text(encoding="utf-8")
    worker = WORKER.read_text(encoding="utf-8")
    status_ctx = STATUS_CTX.read_text(encoding="utf-8")

    if html.count('id="stepBar') != 4:
        errors.append("Wizard UI must have exactly 4 step bars")
    for panel in PANELS:
        if panel not in html:
            errors.append(f"Missing panel {panel}")

    removed_panels = (
        "panelEthernet",
        "panelGuestWifi",
        "panelApDeploy",
        "panelCoin",
        "panelOperator",
    )
    for panel in removed_panels:
        if panel in html:
            errors.append(f"Linear wizard must not include removed panel {panel}")

    phase_fn = prov.find("wizardStepForPhase")
    phase_body = prov[phase_fn:phase_fn + 1200] if phase_fn >= 0 else ""
    for step in WIZARD_STEPS:
        if f'return "{step}"' not in phase_body:
            errors.append(f"wizardStepForPhase must return step {step}")

    removed_steps = ("ethernet", "guest_wifi", "ap_deployment", "coin", "operator")
    for step in removed_steps:
        if f'return "{step}"' in prov:
            errors.append(f"wizardStepLabel must not return removed step {step}")

    if "resumeWizardFromStatus" not in html:
        errors.append("UI must resume wizard from status API")
    if "panelForWizardStep" not in html:
        errors.append("UI must map wizardStep to panels via panelForWizardStep()")
    if "applyWizardStepFromStatus" not in html:
        errors.append("UI must navigate via applyWizardStepFromStatus()")
    if "handleSetupLifecycleError" not in html:
        errors.append("UI must refresh status on SETUP_* lifecycle errors")
    if "clearAllWizardValidation" not in html:
        errors.append("Panel changes must clear stale validation via clearAllWizardValidation()")
    if "queueAutoExistingNetworkScan" not in html:
        errors.append("Save must queue automatic existing-network scan")
    save_handler = html[
        html.find("getElementById('saveRouterBtn').addEventListener") :
        html.find("getElementById('reviewBackBtn').addEventListener")
    ]
    if ("resumeWizardFromStatus" not in save_handler
            and "proceedAfterRouterSave" not in save_handler
            and "showRouterSaveSuccessModal" not in save_handler):
        errors.append("Save handler must resume wizard from backend status after save")
    proceed_fn = html.split("function proceedAfterRouterSave", 1)
    if len(proceed_fn) < 2 or "resumeWizardFromStatus" not in proceed_fn[1].split("function startExistingNetworkScan", 1)[0]:
        errors.append("proceedAfterRouterSave must call resumeWizardFromStatus")
    if "step === 'complete' || isExistingNetworkConfigured" in html:
        errors.append("Resume must not bypass wizardStep with isExistingNetworkConfigured")
    if "step === 'wifi' && isWifiSetupComplete" in html:
        errors.append("Resume must not override backend wifi step with isWifiSetupComplete")

    if "loadNetworkModeStatus" in save_handler:
        errors.append("Save handler must not call loadNetworkModeStatus")

    if "loadSetupStatus" in html and "loadNetworkModeStatus()" in html:
        load_fn = html.find("function loadSetupStatus")
        load_body = html[load_fn : html.find("function ", load_fn + 1)]
        if "loadNetworkModeStatus()" in load_body:
            errors.append("loadSetupStatus must not call loadNetworkModeStatus on boot")

    back_handlers = html.count("showPanel(")
    if back_handlers < 3:
        errors.append("Back navigation must use client-side showPanel")

    if "applyExistingNetworkInFlight" not in html:
        errors.append("Configure flow must track applyExistingNetworkInFlight")
    if "executeAdoption" not in html:
        errors.append("Review panel must configure via executeAdoption()")
    if "isExistingNetworkConfigured" not in html:
        errors.append("Wizard may expose isExistingNetworkConfigured for display only")
    if "restoreAdoptionWorkflowIfNeeded" in html:
        errors.append("Review must not call restoreAdoptionWorkflowIfNeeded")
    if "loadNetworkModeStatus" in html:
        errors.append("loadNetworkModeStatus must be removed")
    if "adoptionWorkflowArmed" in html or "reconnectAdoptionWorkflowFromStatus" in html:
        errors.append("Adoption workflow reconnect logic must be removed")
    show_panel_review = html[html.find("function showPanel") : html.find("function showFormError")]
    if "restoreAdoptionWorkflowIfNeeded" in show_panel_review:
        errors.append("showPanel(panelReview) must not restore adoption workflow")
    if "network-mode" in save_handler and "fetch(" in save_handler:
        errors.append("Save handler must not fetch network-mode")
    if "productionMode" not in prov:
        errors.append("fillSetupStatus must expose productionMode")
    if "wizardStepForPhase" not in prov:
        errors.append("wizard step must derive from apply/configure phase")
    if "SetupStatusContext" not in prov:
        errors.append("fillSetupStatus must accept SetupStatusContext")
    if "fillSetupStatus(JsonObject data, EthernetManager *eth) const" in Path(
        ROOT.parent / "src" / "SetupProvisioningManager.h"
    ).read_text(encoding="utf-8"):
        errors.append("fillSetupStatus must not expose a default-context overload")
    if "buildSetupStatusContext" not in status_ctx:
        errors.append("SetupStatusContext must provide buildSetupStatusContext()")
    if "fillSetupStatus(data, eth)" in worker or "fillSetupStatus(data, _eth)" in worker:
        errors.append("Router worker must not call fillSetupStatus without context")
    if "fillWorkerSetupStatus" not in worker:
        errors.append("Router worker must centralize setup status via fillWorkerSetupStatus()")
    if "buildSetupStatusContext" not in setup:
        errors.append("SetupServer must derive setup status via buildSetupStatusContext()")
    if "provisionedBackBtn" not in html:
        errors.append("Step 5 must include a Back button (provisionedBackBtn)")
    if "setFinishButtonsDisabled" not in html:
        errors.append("Finish must disable Skip/Create to prevent duplicate finish posts")
    if "isWifiConfiguredFromStatus" not in html:
        errors.append("Navigation must gate Step 5 on backend Wi-Fi complete flags")
    finish_idx = setup.find("/api/setup/finish")
    if finish_idx >= 0:
        finish_block = setup[finish_idx : finish_idx + 1800]
        if "finishCompleted()" in finish_block:
            errors.append(
                "Finish HTTP handler must not short-circuit on finishCompleted() "
                "(require installation isReady() only)"
            )
        if "alreadyCompleted" in finish_block and "isReady()" not in finish_block:
            errors.append("Finish alreadyCompleted path must gate on installation isReady()")

    if "ownerDisplayName" not in html:
        errors.append("Owner step must collect full name")
    if 'id="createBtn"' not in html or ">Next</button>" not in html:
        errors.append("Owner step must use Next button")
    if "skipOperatorBtn" not in html or "Skip for Now" not in html:
        errors.append("Setup complete must offer Skip for Now")
    if "finishSetup" not in html:
        errors.append("Setup complete must call finishSetup to enter production mode")
    if "handoffToAdminDashboard" not in html:
        errors.append("Production handoff must route through handoffToAdminDashboard()")
    if "fetchHandoffHealth" not in html:
        errors.append("Production handoff must poll /api/health before redirecting")
    if "waitForDashboardHandoff" not in html:
        errors.append("Production handoff must wait for backend readiness")
    if "data.adminUrl" not in html:
        errors.append("Production handoff must redirect using backend adminUrl")
    if "dashboardRedirectStarted" not in html:
        errors.append("Production handoff must guard against duplicate redirects")
    if "dashboardEntryUrl" in html or "dashboardHealthUrl" in html:
        errors.append("Production handoff must not hardcode dashboard URLs in the frontend")
    if "mode: 'no-cors'" in html:
        errors.append("Production handoff must not use no-cors fetch for readiness checks")
    if "ProductionHandoff::evaluate" not in api:
        errors.append("/api/health must derive ready from ProductionHandoff::evaluate")
    if 'data["adminUrl"]' not in handoff and "adminUrl" not in handoff:
        errors.append("ProductionHandoff must expose adminUrl for dashboard redirect")
    if "registerSetupRoutes" not in web:
        errors.append("Setup plane must register ApiServer setup routes for /api/health")
    if "handoffPhase" not in handoff:
        errors.append("ProductionHandoff must expose completing/production_ready phases")
    if 'checks["owner"]' not in handoff and 'checks["adminApi"]' not in handoff:
        errors.append("ProductionHandoff must expose per-gate checks in /api/health")
    if 'pending.add("admin_api")' not in handoff:
        errors.append("ProductionHandoff must expose pending blockers when ready is false")
    if "showSetupCompletePanel" not in html:
        errors.append("Configure success must navigate to setup complete before production mode")

    removed_ui = (
        "adoptConfirm",
        "createNewNetworkPanel",
        "networkModeSection",
        "previewPlanBtn",
        "applyPlanBtn",
        "changeNetworkModeBtn",
    )
    for token in removed_ui:
        if token in html:
            errors.append(f"Appliance installer must not include legacy UI token {token}")

    if errors:
        for err in errors:
            print(f"setup-wizard-navigation-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("setup-wizard-navigation-check: OK (navigation guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
