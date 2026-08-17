# GUI Visual Closure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the interrupted GUI refinement with compact layouts, thin consistent icons, no redundant hover prompts, and themed call details.

**Architecture:** Keep all controllers and actions unchanged. Refine shared QML primitives first, then Home/Preflight, then Call/Details, with the existing executable-driven GUI contract and fresh rendered screenshots as acceptance.

**Tech Stack:** Qt 6 Quick/QML, C++20 smoke seam, Python 3 contracts, CMake/Ninja, CTest.

## Global Constraints

- Continue only in `/Users/dio/project/ShareMe/.worktrees/gui-refinement` on `codex/gui-refinement`.
- Preserve every existing controller, media, signaling, quality-profile, room, recovery, and accessibility action contract.
- Do not add remote assets, fonts, dependencies, continuous animation, QML polling, blur, or per-frame UI work.
- Keep the external WebRTC cache read-only and exclude builds, screenshots, logs, and local settings from Git.
- Treat automated tests as functional evidence; require fresh rendered screenshots before visual acceptance.

---

### Task 1: Shared controls and icon language

**Files:**
- Modify: `client/tools/rtc_demo/qml/ShareMeTheme.qml`
- Modify: `client/tools/rtc_demo/qml/IconGlyph.qml`
- Modify: `client/tools/rtc_demo/qml/IconControl.qml`
- Modify: `client/tools/rtc_demo/qml/PrimaryButton.qml`
- Modify: `client/tools/rtc_demo/qml/CallTopBar.qml`
- Test: `tests/scripts/gui_qml_contract_test.py`

- [x] Add RED executable/source contracts that reject automatic tooltips and require compact shared-control geometry.
- [x] Run the focused GUI contract and observe the intended failures.
- [x] Implement 17-18 px thin line icons, 40 px tertiary icon controls, compact button geometry, and visible focus without hover bubbles.
- [x] Rebuild the demo and run the focused GUI contract.
- [x] Commit `style: close GUI control language`.

### Task 2: Home and preflight density

**Files:**
- Modify: `client/tools/rtc_demo/qml/HomePage.qml`
- Modify: `client/tools/rtc_demo/qml/PreflightPage.qml`
- Test: `tests/scripts/gui_qml_contract_test.py`

- [x] Add RED contracts for concise copy, absence of “当前意图”, compact action surfaces, and primary-action availability at 760x520.
- [x] Run the Home/Create/Join normal and compact states and observe the intended failures.
- [x] Implement the centered compact workspace and remove duplicate headings/copy without changing inputs or actions.
- [x] Build and run Home/Create/Join contracts at both sizes.
- [x] Commit `style: compact GUI entry flow`.

### Task 3: Active call and details polish

**Files:**
- Modify: `client/tools/rtc_demo/qml/CallControlDock.qml`
- Modify: `client/tools/rtc_demo/qml/CallDetailsDrawer.qml`
- Modify: `client/tools/rtc_demo/qml/CallPage.qml`
- Test: `tests/scripts/gui_qml_contract_test.py`

- [x] Add RED contracts for a 60 px dock, themed meter/slider/disclosure, compact normal summary, and preserved advanced diagnostics.
- [x] Run call-host, call-viewer, and call-host-actions and observe the intended failures.
- [x] Implement the compact dock, themed controls, and simplified normal details while preserving all actions and advanced values.
- [x] Build and run call state/action contracts.
- [x] Commit `style: finish active call surfaces`.

### Task 4: Rendered acceptance, review, and handoff

**Files:**
- Modify: `docs/verification/gui-refinement.md`
- Modify: `docs/development/current-stage.md`
- Modify: this checklist.
- Generated ignored: `build/gui-closure-dev/`, `out/gui-visual-closure/`.

- [ ] Fresh-build the branch and run complete CTest, GUI/QML contracts, GUI smoke, Go race/vet, workflow, validator, portability scan, and `git diff --check`.
- [ ] Capture default Home/Create/Join/Call/Details and logical compact Create screenshots without MotionFixture.
- [ ] Inspect every screenshot for clipping, tooltip bubbles, white native controls, hierarchy, icon consistency, and action visibility; iterate until clean.
- [ ] Complete independent read-only review and fix every Critical/Important issue.
- [ ] Record macOS verified, Windows environment-dependent, and human acoustic/media boundaries truthfully.
- [ ] Commit `docs: finalize GUI visual closure`; push and integrate only after the full gate passes.
