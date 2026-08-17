#!/usr/bin/env python3
"""Regression guards for adoptProgressModal CSS visibility."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
HTML = ROOT.parent / "src" / "web" / "SetupWizardPageHtml.h"


def slice_between(text: str, start: str, end: str) -> str:
    begin = text.find(start)
    if begin < 0:
        return ""
    finish = text.find(end, begin + len(start))
    if finish < 0:
        return text[begin:]
    return text[begin:finish]


def css_rule_index(css: str, selector: str) -> int:
    return css.find(selector)


def main() -> int:
    errors: list[str] = []
    html = HTML.read_text(encoding="utf-8")

    if 'id="adoptProgressModal"' not in html:
        errors.append("Setup wizard must define adoptProgressModal")
    if 'class="modal-backdrop hidden-block"' not in html.replace(" ", ""):
        if 'class="modal-backdrop hidden-block"' not in html:
            if "modal-backdrop hidden-block" not in html:
                errors.append("adoptProgressModal must start hidden with modal-backdrop hidden-block")

    style_start = html.find("<style>")
    style_end = html.find("</style>")
    if style_start < 0 or style_end < 0:
        errors.append("Setup wizard page must include inline <style> block")
    else:
        css = html[style_start:style_end]
        backdrop_idx = css_rule_index(css, ".modal-backdrop{")
        if backdrop_idx < 0:
            backdrop_idx = css_rule_index(css, ".modal-backdrop ")
        hidden_combo_idx = css_rule_index(css, ".modal-backdrop.hidden-block")
        if hidden_combo_idx < 0:
            errors.append("CSS must define .modal-backdrop.hidden-block to override display:flex")
        elif backdrop_idx >= 0 and hidden_combo_idx <= backdrop_idx:
            errors.append(".modal-backdrop.hidden-block must be declared after .modal-backdrop")
        else:
            combo_rule = css[hidden_combo_idx:hidden_combo_idx + 120]
            if "display:none" not in combo_rule.replace(" ", ""):
                errors.append(".modal-backdrop.hidden-block must set display:none")

    hide_fn = slice_between(html, "function hideAdoptProgressModal(", "function showAdoptProgressModal(")
    show_fn = slice_between(html, "function showAdoptProgressModal(", "function hideAdoptPreviewModal(")
    show_panel_fn = slice_between(html, "function showPanel(", "function showFormError(")
    save_handler = html[html.find("saveRouterBtn") : html.find("reviewBackBtn")]

    if not hide_fn:
        errors.append("hideAdoptProgressModal() must exist")
    elif "classList.add('hidden-block')" not in hide_fn:
        errors.append("hideAdoptProgressModal() must add hidden-block class")

    if not show_fn:
        errors.append("showAdoptProgressModal() must exist")
    elif "classList.remove('hidden-block')" not in show_fn:
        errors.append("showAdoptProgressModal() must remove hidden-block class")

    if "panelReview" not in html or "adoptProgressModal" not in html:
        errors.append("adoptProgressModal must live inside panelReview markup")
    else:
        panel_start = html.find('id="panelReview"')
        panel_end = html.find('id="panelProvisioned"', panel_start)
        panel_body = html[panel_start:panel_end if panel_end > 0 else len(html)]
        if "adoptProgressModal" not in panel_body:
            errors.append("adoptProgressModal must remain nested in panelReview")

    if show_panel_fn:
        review_branch = show_panel_fn[show_panel_fn.find("name === 'panelReview'") :]
        if "showAdoptProgressModal(" in review_branch:
            errors.append("showPanel(panelReview) must not call showAdoptProgressModal()")
    else:
        errors.append("showPanel() must exist")

    if "showAdoptProgressModal(" in save_handler:
        errors.append("Save handler must not call showAdoptProgressModal() before Start Adoption")

    scan_done_fn = slice_between(html, "function handleExistingScanJobDone(", "var PANEL_ORDER")
    render_results_fn = slice_between(html, "function renderExistingScanResults(", "function confirmExistingScanRestart(")
    for fn_name, body in (
        ("handleExistingScanJobDone", scan_done_fn),
        ("renderExistingScanResults", render_results_fn),
    ):
        if not body:
            errors.append(f"{fn_name}() must exist")
        elif "showAdoptProgressModal(" in body:
            errors.append(f"{fn_name}() must not open adoptProgressModal")

    execute_fn = slice_between(html, "function executeAdoption(", "function formatStageLabel(")
    if not execute_fn or "showAdoptProgressModal(" not in execute_fn:
        errors.append("executeAdoption() must be the Start Adoption entry that shows the modal")

    if errors:
        for err in errors:
            print(f"setup-wizard-adopt-progress-modal-check: FAIL — {err}", file=sys.stderr)
        return 1

    print("setup-wizard-adopt-progress-modal-check: OK (modal visibility guards passed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
